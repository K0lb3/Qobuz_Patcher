const fs = require("fs");
const { execSync } = require("child_process");
const path = require("path");

const rootDir = path.join(__dirname, "..");
const appDir = path.join(rootDir, "app"); // Adjust 'app' to your subpackage folder name if needed
const qobuzSrc = path.join(appDir, "node_modules", "@qobuz");
const qobuzDest = path.join(rootDir, "node_modules", "@qobuz");
const packageJsonPath = path.join(appDir, "package.json");

function runWorkflow() {
  try {
    console.log("1. Moving @qobuz out of app/node_modules...");
    if (fs.existsSync(qobuzSrc)) {
      fs.mkdirSync(path.dirname(qobuzDest), { recursive: true });
      if (fs.existsSync(qobuzDest)) {
        fs.rmSync(qobuzDest, { recursive: true, force: true });
      }
      fs.renameSync(qobuzSrc, qobuzDest);
    }

    console.log("2. Removing @qobuz/qobuz-dwp-ui from app/package.json...");
    const pkgRaw = fs.readFileSync(packageJsonPath, "utf8");
    const pkg = JSON.parse(pkgRaw);
    const dwpUiVersion = pkg.dependencies?.["@qobuz/qobuz-dwp-ui"];

    if (pkg.dependencies && "@qobuz/qobuz-dwp-ui" in pkg.dependencies) {
      delete pkg.dependencies["@qobuz/qobuz-dwp-ui"];
    }
    fs.writeFileSync(packageJsonPath, JSON.stringify(pkg, null, 2), "utf8");

    console.log("3. Deleting app/node_modules...");
    const appNodeModules = path.join(appDir, "node_modules");
    if (fs.existsSync(appNodeModules)) {
      fs.rmSync(appNodeModules, { recursive: true, force: true });
    }

    console.log("4. Rebuilding node_modules with electron...");
    execSync("npm install", { cwd: appDir, stdio: "inherit" });
    execSync("npx electron-rebuild --version 45.0.0", {
      cwd: appDir,
      stdio: "inherit",
    });

    console.log("5. Re-adding @qobuz/qobuz-dwp-ui to app/package.json...");
    const pkgUpdatedRaw = fs.readFileSync(packageJsonPath, "utf8");
    const pkgUpdated = JSON.parse(pkgUpdatedRaw);
    pkgUpdated.dependencies = pkgUpdated.dependencies || {};
    if (dwpUiVersion) {
      pkgUpdated.dependencies["@qobuz/qobuz-dwp-ui"] = dwpUiVersion;
    }
    fs.writeFileSync(
      packageJsonPath,
      JSON.stringify(pkgUpdated, null, 2),
      "utf8",
    );

    console.log("6. Moving @qobuz back to app/node_modules...");
    if (fs.existsSync(qobuzDest)) {
      fs.mkdirSync(qobuzSrc, { recursive: true });
      // Move contents back
      const items = fs.readdirSync(qobuzDest);
      for (const item of items) {
        fs.renameSync(path.join(qobuzDest, item), path.join(qobuzSrc, item));
      }
      fs.rmSync(qobuzDest, { recursive: true, force: true });
    }

    console.log(
      "✅ Sub-package build and rebuild workflow completed successfully!",
    );
  } catch (error) {
    console.error("❌ Workflow failed:", error);
    process.exit(1);
  }
}

runWorkflow();
