const { chromium } = require('playwright');

const url = process.argv[2] || 'http://localhost:8080';
const output = process.argv[3] || 'screenshot.png';

(async () => {
  let browser;

  try {
    browser = await chromium.launch({
      headless: true,
    });

    const page = await browser.newPage({
      viewport: {
        width: 1280,
        height: 720,
      },
      deviceScaleFactor: 1,
    });

    await page.goto(url, {
      waitUntil: 'networkidle',
      timeout: 60_000,
    });

    await page.screenshot({
      path: output,
      fullPage: true,
    });

    console.log(`截图完成：${output}`);
  } catch (error) {
    console.error('截图失败：', error.message);
    process.exitCode = 1;
  } finally {
    if (browser) {
      await browser.close();
    }
  }
})();
