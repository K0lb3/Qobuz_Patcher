const fs = require("fs");
const path = require("path");

// Target file path
const projectPath = path.join(__dirname, "..");
const filePath = path.join(projectPath, "app", "main-win32.js");

// Choose audioplayer via command-line argument: 'miniaudio' (default) or 'electron'
// Usage: node patch.js miniaudio  OR  node patch.js electron
let audioplayer;
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  if ((args[i] === "--audioplayer" || args[i] === "-ap") && args[i + 1]) {
    audioplayer = args[i + 1];
    i++; // skip next element
  }
}

// Early validation of the audioplayer argument
if (audioplayer === undefined) {
  console.error(
    "[!] Error: No audioplayer specified via --audioplayer (electron, miniaudio)",
  );
  process.exit(1);
}
if (audioplayer !== "electron" && audioplayer !== "miniaudio") {
  console.error(`[!] Error: Invalid audioplayer '${audioplayer}'. Choose 'electron' or 'miniaudio'.`);
  process.exit(1);
}

if (!fs.existsSync(filePath)) {
  console.error(`[!] Error: Could not find target file at ${filePath}`);
  process.exit(1);
}

let content = fs.readFileSync(filePath, "utf8");
let modified = false;

// 1. Patch Tray icon: .ico -> .png
const trayRegex =
  /new\s+t\.Tray\s*\(\s*`\$\{r\.default\.APP_ICON\}\.ico`\s*\)/g;
if (trayRegex.test(content)) {
  content = content.replace(
    trayRegex,
    "new t.Tray(`${r.default.APP_ICON}.png`)",
  );
  console.log("[+] Patched Tray icon: .ico -> .png");
  modified = true;
} else {
  console.log("[-] Tray icon pattern not found or already patched.");
}

// 2. Patch Native Module / dlopen line
const dlopenRegex =
  /process\.dlopen\s*\(\s*e\s*,\s*__dirname\s*\+\s*["']\/["']\s*\+\s*i\.p\s*\+\s*["']juce\.node["']\s*\)\s*/g;

if (dlopenRegex.test(content)) {
  // Reset regex index if using .test() before .replace() with global flag
  dlopenRegex.lastIndex = 0;

  if (audioplayer === "electron") {
    content = content.replace(
      dlopenRegex,
      'e.exports = require(__dirname + "/" + i.p + "ap_electron.js")',
    );
    console.log(
      "[+] Patched dlopen: Switched to Electron audioplayer (ap_electron.js)",
    );
    modified = true;
  } else if (audioplayer === "miniaudio") {
    content = content.replace(
      dlopenRegex,
      'process.dlopen(e, __dirname + "/" + i.p + "miniaudio.node")',
    );
    console.log(
      "[+] Patched dlopen: Switched to Miniaudio audioplayer (miniaudio.node)",
    );
    modified = true;
  }
} else {
  console.log("[-] juce.node dlopen pattern not found or already patched.");
}

// 3. Patch DEBUG_TOOLS: !1 -> 1
const debugToolsRegex = /DEBUG_TOOLS\s*:\s*!1/g;
if (debugToolsRegex.test(content)) {
  content = content.replace(debugToolsRegex, "DEBUG_TOOLS: 1");
  console.log("[+] Patched DEBUG_TOOLS: !1 -> 1");
  modified = true;
} else {
  console.log("[-] DEBUG_TOOLS pattern not found or already patched.");
}

// 4. Copy related audioplayer files
if (audioplayer === "electron") {
  fs.copyFileSync(
    path.join(projectPath, "audioplayer", "ap_electron.js"),
    path.join(projectPath, "app", "ap_electron.js"),
  );
  console.log("[✔] Copied ap_electron.js!");
} else if (audioplayer === "miniaudio") {
  const miniaudioNodeFp = path.join(
    projectPath,
    "audioplayer",
    "miniaudio",
    "build",
    "miniaudio.node",
  );
  if (fs.existsSync(miniaudioNodeFp)) {
    fs.copyFileSync(
      miniaudioNodeFp,
      path.join(projectPath, "app", "miniaudio.node"),
    );
    console.log("[✔] Copied miniaudio.node!");
  } else {
    console.error("[!] Miniaudio wasn't built yet!");
  }
}

// Save changes back to the file if modifications were made
if (modified) {
  fs.writeFileSync(filePath, content, "utf8");
  console.log("[✔] main-win32.js successfully patched!");
} else {
  console.log("[ℹ] No changes were made to main-win32.js (files may already be patched).");
}