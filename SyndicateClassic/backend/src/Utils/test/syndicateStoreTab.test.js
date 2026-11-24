import assert from "node:assert/strict";
import test from "node:test";
import fs from "node:fs";

// hotfixes.js needs dotenv (not installed where these tests run), so its text is read directly.
const hotfixes = fs.readFileSync(new URL("../hotfixes.js", import.meta.url), "utf8");
const runtimeOptions = hotfixes.split("export const defaultruntimeoptions")[1].split("`;")[0];

test("the lobby's STORE tab is not hidden by the runtime options hotfix", () => {
  assert.ok(runtimeOptions.includes("!DisabledFrontendNavigationTabs=ClearArray"), "the game's own hidden tabs are cleared");
  assert.doesNotMatch(runtimeOptions, /^\+DisabledFrontendNavigationTabs=/m, "no lobby tab is hidden or disabled");
  assert.doesNotMatch(runtimeOptions, /^[^;\n]*TabName="AthenaStore"/m);
});
