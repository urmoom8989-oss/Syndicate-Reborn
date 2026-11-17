// Syndicate: the backend writes its own log file instead of relying on the launcher reading its output.
//
// The launcher used to read this process's output through a pipe and copy it to
// Runtime/Server/backend-startup.log. When that launcher closed or restarted while the backend kept
// running, the pipe broke: the next log line failed (EPIPE) and the backend stopped, or got stuck
// reporting its own error, and every open Fortnite was sent back to the Epic login screen.
// Now: with TILTED_BACKEND_LOG set (the launcher sets it), every console line is appended to that file
// directly, and a broken or missing stdout/stderr is ignored. This module must be imported first.
import fs from "fs";
import util from "util";

const logPath = process.env.TILTED_BACKEND_LOG || "";
let fileOk = Boolean(logPath);
let consoleOk = true;

// A closed pipe or console must never stop the backend.
for (const stream of [process.stdout, process.stderr]) {
  try { stream.on("error", () => { consoleOk = false; }); } catch {}
}

function toFile(text) {
  if (!fileOk) return;
  // eslint-disable-next-line no-control-regex
  try { fs.appendFileSync(logPath, text.replace(/\x1b\[[0-9;]*m/g, "") + "\n", "utf8"); } // colour codes stripped
  catch { fileOk = false; } // a locked or missing log never stops the backend either
}

for (const method of ["log", "info", "warn", "error", "debug"]) {
  const original = console[method].bind(console);
  console[method] = (...args) => {
    let text;
    try { text = util.format(...args); } catch { text = String(args[0]); }
    toFile(text);
    if (!consoleOk) return;
    try { original(...args); } catch { consoleOk = false; }
  };
}

export function isBrokenPipe(error) {
  return Boolean(error && (error.code === "EPIPE" || error.code === "ERR_STREAM_DESTROYED" || error.code === "EOF"));
}
