const fs = require("fs");
const path = require("path");
const { execSync } = require("child_process");

// Define directories relative to your project workspace
const WORK_DIR = path.join(__dirname, "../work-dir");
const APP_DEST = path.join(__dirname, "../app");
const LOCAL_PACKAGE_JSON = path.join(APP_DEST, "package.json");

async function updateQobuz() {
  console.log("🔍 Checking for the latest Qobuz desktop version...");

  // 1. Fetch Qobuz apps page
  const res = await fetch("https://www.qobuz.com/apps");
  const html = await res.text();

  // 2. Look for the Windows installer URL using regex pattern matching
  const regex =
    /https:\/\/desktop\.qobuz\.com\/releases\/win32\/x64\/windows7_8_10\/([^\/]+)\/Qobuz_Installer\.exe/;
  const match = html.match(regex);

  if (!match) {
    console.error(
      "❌ Could not locate the Qobuz Windows installer URL on the page.",
    );
    process.exit(1);
  }

  const remoteUrl = match[0];
  const remoteVersion = match[1];
  console.log(`✨ Latest remote version found: ${remoteVersion}`);

  // 3. Compare version with local one from /app/package.json
  let localVersion = "";
  if (fs.existsSync(LOCAL_PACKAGE_JSON)) {
    try {
      const pkgData = JSON.parse(fs.readFileSync(LOCAL_PACKAGE_JSON, "utf8"));
      localVersion = pkgData.version || "";
    } catch (e) {
      console.warn(
        "⚠️ Could not parse existing app/package.json, treating as no local version.",
      );
    }
  }

  if (localVersion === remoteVersion && fs.existsSync(APP_DEST)) {
    console.log(
      `✅ Local version (${localVersion}) is already up to date. Exiting.`,
    );
    // 👉 Force Node to terminate immediately once everything is done
    process.exit(0);
  }

  console.log(
    `🚀 Update available! (Local: ${localVersion || "None"} -> Remote: ${remoteVersion})`,
  );

  // Ensure work directory exists
  if (!fs.existsSync(WORK_DIR)) {
    fs.mkdirSync(WORK_DIR, { recursive: true });
  }

  const installerPath = path.join(WORK_DIR, "Qobuz_Installer.exe");

  // 4 & 5. Download installer to work directory
  console.log(`📥 Downloading installer from ${remoteUrl} ...`);
  const installerRes = await fetch(remoteUrl);
  const buffer = Buffer.from(await installerRes.arrayBuffer());
  fs.writeFileSync(installerPath, buffer);
  console.log(`📦 Saved installer to ${installerPath}`);

  // 6. Unpack installer via 7z
  console.log(`📂 Unpacking installer with 7z...`);
  execSync(`7z x "${installerPath}" -y -o"${WORK_DIR}"`, { stdio: "inherit" });

  // 7. Find the .nupkg file name dynamically
  const files = fs.readdirSync(WORK_DIR);
  const nupkgFile = files.find((f) => f.endsWith(".nupkg"));

  if (!nupkgFile) {
    console.error("❌ Could not find the .nupkg file after extraction.");
    process.exit(1);
  }

  const nupkgPath = path.join(WORK_DIR, nupkgFile);
  console.log(`📦 Found package: ${nupkgFile}`);

  // 8. Unpack the .nupkg via 7z
  const nupkgExtractDir = path.join(WORK_DIR, "nupkg_extracted");
  console.log(`📂 Unpacking .nupkg archive...`);
  execSync(`7z x "${nupkgPath}" -y -o"${nupkgExtractDir}"`, {
    stdio: "inherit",
  });

  // 9. Copy the app folder to workspace root /app
  const sourceAppPath = path.join(nupkgExtractDir, "lib/net45/resources/app");

  if (!fs.existsSync(sourceAppPath)) {
    console.error(
      `❌ Expected target app source path not found: ${sourceAppPath}`,
    );
    process.exit(1);
  }

  if (fs.existsSync(APP_DEST)) {
    fs.rmSync(APP_DEST, { recursive: true, force: true });
  }

  console.log(`📋 Copying app files to ${APP_DEST} ...`);
  fs.cpSync(sourceAppPath, APP_DEST, { recursive: true });

  console.log(
    `🎉 Qobuz app successfully updated and patched to version ${remoteVersion}!`,
  );

  // 👉 Force Node to terminate immediately once everything is done
  process.exit(0);
}

updateQobuz().catch((err) => {
  console.error("❌ Error during update process:", err);
  process.exit(1);
});
