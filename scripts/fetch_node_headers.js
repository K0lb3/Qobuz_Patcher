const fs = require("fs");
const path = require("path");

async function fetchHeaders() {
  // Automatically uses current Node version, or allows overriding for a specific Electron target version
  const nodeVersion = process.env.TARGET_NODE_VERSION || process.versions.node;
  const tag = `v${nodeVersion}`;

  console.log(`🔍 Fetching official Node-API headers for version: ${tag}`);

  const files = [
    "src/node_api.h",
    "src/js_native_api.h",
    "src/js_native_api_types.h",
    "src/node_api_types.h",
  ];

  const includeDir = path.join(
    __dirname,
    "..",
    "audioplayer",
    "miniaudio_node",
    "include",
  );
  if (!fs.existsSync(includeDir)) {
    fs.mkdirSync(includeDir, { recursive: true });
  }

  for (const file of files) {
    const url = `https://raw.githubusercontent.com/nodejs/node/${tag}/${file}`;
    const fileName = path.basename(file);
    const dest = path.join(includeDir, fileName);

    console.log(`📥 Downloading ${fileName}...`);
    const res = await fetch(url);

    if (!res.ok) {
      throw new Error(
        `Failed to fetch ${file} for tag ${tag}: ${res.statusText}`,
      );
    }

    const text = await res.text();
    fs.writeFileSync(dest, text, "utf8");
  }

  console.log("✅ Node-API headers updated successfully!");
  process.exit(0);
}

fetchHeaders().catch((err) => {
  console.error("❌ Error fetching headers:", err);
  process.exit(1);
});
