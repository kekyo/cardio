// Run the PlatformIO test image on Espressif's QEMU and report the Unity result.
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { once } from "node:events";
import { homedir, tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";

const build = process.argv[2];
const { target, idf_path } = JSON.parse(await readFile(join(build, "project_description.json"), "utf8"));
const { flash_settings, flash_files, bootloader, "partition-table": partition_table, app } =
  JSON.parse(await readFile(join(build, "flasher_args.json"), "utf8"));
const machines = { esp32: "xtensa", esp32s3: "xtensa", esp32c3: "riscv32" };
if (!Object.hasOwn(machines, target)) throw new Error(`Unsupported QEMU target: ${target}`);

// ESP-IDF supplies the flash layout. PlatformIO supplies the three binaries.
const size = /^(\d+)MB$/.exec(flash_settings.flash_size);
if (!size) throw new Error(`Unexpected flash size: ${flash_settings.flash_size}`);
const flash = Buffer.alloc(Number(size[1]) * 1024 * 1024, 0xff);
const filenames = new Map([[bootloader.file, "bootloader.bin"],
  [partition_table.file, "partitions.bin"], [app.file, "firmware.bin"]]);
for (const [offset, file] of Object.entries(flash_files)) {
  const binary = await readFile(join(build, filenames.get(file) ?? file));
  flash.set(binary, Number(offset));
}
// Model the silicon revisions used by ESP-IDF's QEMU launcher (v6.0,
// tools/idf_py_actions/qemu_ext.py). C3 firmware defaults to revision 0.3.
const efuse = Buffer.alloc(target === "esp32" ? 124 : 1024);
if (target === "esp32") {
  efuse[13] = 0x80;
  efuse[22] = 0x10;
} else {
  efuse[38] = 0x0c;
}
const arch = machines[target];
const tools = JSON.parse(await readFile(join(idf_path, "tools", "tools.json"), "utf8")).tools;
const tool = tools.find(tool => tool.name === `qemu-${arch}`);
const version = tool.versions.find(version => version.status === "recommended").name;
const binary = `qemu-system-${arch}`;
const toolsRoot = process.env.IDF_TOOLS_PATH || join(homedir(), ".espressif");
const installed = join(toolsRoot, "tools", tool.name, version, ...tool.export_paths[0], binary);
const dir = await mkdtemp(join(tmpdir(), "cardio-qemu-"));
try {
  await writeFile(join(dir, "flash.bin"), flash);
  await writeFile(join(dir, "efuse.bin"), efuse);
  const qemu = spawn(existsSync(installed) ? installed : binary, [
    "-M", target, "-nographic", "-serial", "mon:stdio", "-nic", "none",
    "-drive", "file=flash.bin,if=mtd,format=raw",
    "-drive", "file=efuse.bin,if=none,format=raw,id=efuse",
    "-global", `driver=nvram.${target}.efuse,property=drive,value=efuse`,
    "-global", `driver=timer.${target}.timg,property=wdt_disable,value=true`,
  ], { cwd: dir, stdio: ["ignore", "pipe", "inherit"] });
  let summaryPassed = false;
  let passed = false;
  let panic = false;
  const stop = () => qemu.kill();
  process.once("SIGINT", stop);
  process.once("SIGTERM", stop);
  const timeout = setTimeout(() => {
    console.error("QEMU timed out waiting for the test result");
    qemu.kill("SIGKILL");
  }, 90000);
  try {
    createInterface({ input: qemu.stdout }).on("line", line => {
      console.log(line);
      if (/Guru Meditation Error|abort\(\) was called|assert failed:/.test(line)) panic = true;
      if (panic && /Backtrace:|Rebooting/.test(line)) stop();
      const summary = /^(\d+) Tests (\d+) Failures (\d+) Ignored\s*$/.exec(line);
      if (summary) {
        summaryPassed = Number(summary[1]) > 0 && summary[2] === "0" && summary[3] === "0";
        if (!summaryPassed) stop();
      }
      if (summaryPassed && line.trim() === "OK") {
        passed = true;
        stop();
      }
    });
    await once(qemu, "close");
    if (!passed || panic) process.exitCode = 1;
  } finally {
    clearTimeout(timeout);
    process.off("SIGINT", stop);
    process.off("SIGTERM", stop);
  }
} finally {
  await rm(dir, { recursive: true, force: true });
}
