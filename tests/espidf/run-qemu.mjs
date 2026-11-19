// Run the PlatformIO test image on Espressif's QEMU and report the Unity result.
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { createInterface } from "node:readline";

const build = process.argv[2];
const { target } = JSON.parse(await readFile(join(build, "project_description.json"), "utf8"));
const { flash_settings, bootloader, "partition-table": partition_table, app } =
  JSON.parse(await readFile(join(build, "flasher_args.json"), "utf8"));
const machines = { esp32: "xtensa", esp32s3: "xtensa", esp32c3: "riscv32" };
if (!Object.hasOwn(machines, target)) throw new Error(`Unsupported QEMU target: ${target}`);

// ESP-IDF supplies the flash layout. PlatformIO supplies the three binaries.
const size = /^(\d+)MB$/.exec(flash_settings.flash_size);
if (!size) throw new Error(`Unexpected flash size: ${flash_settings.flash_size}`);
const flash = Buffer.alloc(Number(size[1]) * 1024 * 1024, 0xff);
for (const [part, file] of [[bootloader, "bootloader.bin"], [partition_table, "partitions.bin"], [app, "firmware.bin"]]) {
  const binary = await readFile(join(build, file));
  flash.set(binary, Number(part.offset));
}
const image = join(build, "cardio-qemu.bin");
await writeFile(image, flash);
const qemu = spawn(`qemu-system-${machines[target]}`, [
  "-M", target, "-nographic", "-serial", "mon:stdio",
  "-nic", "none", "-drive", `file=${image},if=mtd,format=raw,snapshot=on`,
  "-global", `driver=timer.${target}.timg,property=wdt_disable,value=true`,
], { stdio: ["ignore", "pipe", "inherit"] });

let passed = false;
const timeout = setTimeout(() => {
  console.error("QEMU timed out waiting for the test result");
  qemu.kill("SIGKILL");
}, 90000);
const lines = createInterface({ input: qemu.stdout });
lines.on("line", line => {
  console.log(line);
  const summary = /^(\d+) Tests (\d+) Failures (\d+) Ignored\s*$/.exec(line);
  if (summary) {
    passed = Number(summary[1]) > 0 && summary[2] === "0" && summary[3] === "0";
    qemu.kill();
  } else if (/Guru Meditation Error|abort\(\) was called|assert failed:/.test(line)) {
    qemu.kill();
  }
});
qemu.on("error", error => console.error(error.message));
qemu.on("close", () => {
  clearTimeout(timeout);
  process.exitCode = passed ? 0 : 1;
});
