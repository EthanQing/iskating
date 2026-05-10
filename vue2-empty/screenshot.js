const { chromium } = require('playwright');

// 用法：
// 1. 整页截图：node screenshot.js http://localhost:8080 screenshot.png
// 2. 元素截图：node screenshot.js http://localhost:8080 app.png '#app'
// 3. 伪元素截图：node screenshot.js http://localhost:8080 qt_assets/topbar_after.png 'header.topbar::after'
// 4. 指定伪元素裁剪高度：node screenshot.js http://localhost:8080 qt_assets/topbar_after.png 'header.topbar::after' --height=2
// 5. 保留页面原背景：node screenshot.js http://localhost:8080 qt_assets/topbar_after.png 'header.topbar::after' --bg=keep
// 6. 指定底色，避免透明图片在某些查看器里显示黑色：node screenshot.js http://localhost:8080 qt_assets/topbar_after.png 'header.topbar::after' --bg=#222
const url = process.argv[2] || 'http://localhost:8080';
const output = process.argv[3] || 'screenshot.png';
const selector = process.argv[4];
const extraArgs = process.argv.slice(5);

const options = {
  height: undefined,
  // pseudo 截图默认会把 header/html/body 背景改透明，避免把黑色 topbar 背景一起截进去。
  // 可选：transparent | keep | 任意 CSS 颜色，例如 #222、white、rgba(0,0,0,0)
  bg: 'transparent',
};

for (const arg of extraArgs) {
  if (/^\d+(\.\d+)?$/.test(arg)) {
    options.height = Number(arg);
  } else if (arg.startsWith('--height=')) {
    options.height = Number(arg.slice('--height='.length));
  } else if (arg.startsWith('--bg=')) {
    options.bg = arg.slice('--bg='.length);
  }
}

function getPseudoInfo(selector) {
  if (!selector) return null;

  if (selector.endsWith('::after')) {
    return {
      type: '::after',
      baseSelector: selector.slice(0, -'::after'.length),
    };
  }

  if (selector.endsWith('::before')) {
    return {
      type: '::before',
      baseSelector: selector.slice(0, -'::before'.length),
    };
  }

  return null;
}

function isNumber(value) {
  return Number.isFinite(value) && !Number.isNaN(value);
}

function clampClipToViewport(clip, viewport) {
  const x = Math.max(0, clip.x);
  const y = Math.max(0, clip.y);
  const maxWidth = viewport.width - x;
  const maxHeight = viewport.height - y;

  return {
    x,
    y,
    width: Math.max(1, Math.min(clip.width, maxWidth)),
    height: Math.max(1, Math.min(clip.height, maxHeight)),
  };
}

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

    if (selector) {
      const pseudoInfo = getPseudoInfo(selector);

      if (pseudoInfo) {
        const element = page.locator(pseudoInfo.baseSelector).first();

        await element.waitFor({
          state: 'visible',
          timeout: 30_000,
        });

        await element.scrollIntoViewIfNeeded();

        const box = await element.boundingBox();

        if (!box) {
          throw new Error(`没有找到元素：${pseudoInfo.baseSelector}`);
        }

        // 如果 ::after 画的是透明渐变白线，直接截图 header 底部会把 header 的黑色背景一起截进去。
        // 所以默认把当前元素和页面背景临时改透明；如果需要保留原页面背景，传 --bg=keep。
        if (options.bg !== 'keep') {
          await element.evaluate((el, bg) => {
            el.dataset.screenshotOldBackground = el.style.background;
            el.dataset.screenshotOldBackgroundColor = el.style.backgroundColor;
            el.style.setProperty('background', 'transparent', 'important');
            el.style.setProperty('background-color', 'transparent', 'important');

            document.documentElement.dataset.screenshotOldBackground = document.documentElement.style.background;
            document.body.dataset.screenshotOldBackground = document.body.style.background;

            if (bg === 'transparent') {
              document.documentElement.style.setProperty('background', 'transparent', 'important');
              document.body.style.setProperty('background', 'transparent', 'important');
            } else {
              document.documentElement.style.setProperty('background', bg, 'important');
              document.body.style.setProperty('background', bg, 'important');
            }
          }, options.bg);
        }

        const pseudoBox = await element.evaluate((el, pseudoType, fallbackHeight) => {
          const style = window.getComputedStyle(el, pseudoType);
          const rect = el.getBoundingClientRect();

          const px = (value) => {
            if (!value || value === 'auto') return undefined;
            const parsed = Number.parseFloat(value);
            return Number.isFinite(parsed) ? parsed : undefined;
          };

          const width = px(style.width) || rect.width;
          const borderTop = px(style.borderTopWidth) || 0;
          const borderBottom = px(style.borderBottomWidth) || 0;
          const styleHeight = px(style.height) || 0;
          const height = fallbackHeight || styleHeight || borderTop + borderBottom || 2;

          const left = px(style.left);
          const right = px(style.right);
          const top = px(style.top);
          const bottom = px(style.bottom);

          let x;
          if (left !== undefined) {
            x = rect.x + left;
          } else if (right !== undefined) {
            x = rect.x + rect.width - right - width;
          } else {
            x = rect.x;
          }

          let y;
          if (top !== undefined) {
            y = rect.y + top;
          } else if (bottom !== undefined) {
            y = rect.y + rect.height - bottom - height;
          } else {
            y = pseudoType === '::after' ? rect.y + rect.height - height : rect.y;
          }

          // 处理常见写法：left: 50%; transform: translateX(-50%);
          if (style.transform && style.transform !== 'none') {
            const match = style.transform.match(/matrix\(([^)]+)\)/);
            if (match) {
              const parts = match[1].split(',').map((item) => Number.parseFloat(item.trim()));
              if (parts.length === 6) {
                x += parts[4] || 0;
                y += parts[5] || 0;
              }
            }
          }

          return {
            x,
            y,
            width,
            height,
            content: style.content,
            background: style.background,
            backgroundImage: style.backgroundImage,
          };
        }, pseudoInfo.type, options.height);

        if (!isNumber(pseudoBox.x) || !isNumber(pseudoBox.y) || !isNumber(pseudoBox.width) || !isNumber(pseudoBox.height)) {
          throw new Error(`无法计算伪元素位置：${selector}`);
        }

        const viewport = page.viewportSize();
        const clip = clampClipToViewport({
          x: pseudoBox.x,
          y: pseudoBox.y,
          width: pseudoBox.width,
          height: pseudoBox.height,
        }, viewport);

        await page.screenshot({
          path: output,
          clip,
          omitBackground: options.bg === 'transparent',
        });

        console.log(`伪元素截图完成：${output}`);
        console.log(`选择器：${selector}`);
        console.log(`裁剪区域：x=${clip.x}, y=${clip.y}, width=${clip.width}, height=${clip.height}`);
        console.log(`背景模式：${options.bg}`);
      } else {
        const element = page.locator(selector).first();

        await element.waitFor({
          state: 'visible',
          timeout: 30_000,
        });

        await element.screenshot({
          path: output,
        });

        console.log(`元素截图完成：${output}，选择器：${selector}`);
      }
    } else {
      await page.screenshot({
        path: output,
        fullPage: true,
      });

      console.log(`整页截图完成：${output}`);
    }
  } catch (error) {
    console.error('截图失败：', error.message);
    process.exitCode = 1;
  } finally {
    if (browser) {
      await browser.close();
    }
  }
})();
