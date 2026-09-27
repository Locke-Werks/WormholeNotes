// Tells WormholeNotes which page is showing and which tabs are open. It only
// ever sends URLs and titles, only to WormholeNotes on this machine, through
// the browser's native messaging, and keeps nothing.

const HOST = "com.lockewerks.wormholenotes";
let port = null;

function connect() {
  try {
    port = chrome.runtime.connectNative(HOST);
    port.onDisconnect.addListener(() => {
      port = null;
    });
  } catch (e) {
    port = null;
  }
}

function send(message) {
  if (!port) connect();
  if (!port) return;
  try {
    port.postMessage(message);
  } catch (e) {
    port = null;
  }
}

async function report() {
  const tabs = await chrome.tabs.query({});
  const focused = await chrome.windows.getLastFocused({}).catch(() => null);
  const active = tabs.find((t) => t.active && focused && t.windowId === focused.id);
  send({
    active: active ? { url: active.url || "", title: active.title || "" } : null,
    // Every window's showing tab, so a window that is not focused still
    // knows its page.
    showing: tabs.filter((t) => t.active).map((t) => ({ url: t.url || "", title: t.title || "" })),
    tabs: tabs.map((t) => ({ url: t.url || "", title: t.title || "" })),
  });
}

// Tab titles and URLs settle a moment after the events that change them, and
// a burst of events should cost one report.
let pending = null;
function soon() {
  clearTimeout(pending);
  pending = setTimeout(report, 150);
}

chrome.tabs.onActivated.addListener(soon);
chrome.tabs.onUpdated.addListener((id, change) => {
  if (change.url || change.title || change.status === "complete") soon();
});
chrome.tabs.onRemoved.addListener(soon);
chrome.tabs.onCreated.addListener(soon);
chrome.windows.onFocusChanged.addListener(soon);
chrome.runtime.onStartup.addListener(soon);
chrome.runtime.onInstalled.addListener(soon);
soon();
