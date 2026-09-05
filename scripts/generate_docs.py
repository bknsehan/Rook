#!/usr/bin/env python3
"""
generate_docs.py
Generates the comprehensive, modern, distraction-free Rook Documentation & Beginner Course
(docs/rook-language-guide.html) for Rook & Rokade v0.5.0.
"""

import os
import sys

# Ensure local script directory is in python path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOK_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_FILE = os.path.join(ROOK_ROOT, "docs", "rook-language-guide.html")

sys.path.insert(0, SCRIPT_DIR)
from docs_guide import get_guide_chapters
from docs_beginner import get_beginner_modules

def make_code_box(lang, code, title=""):
    display_title = title if title else lang.upper()
    title_bar = (
        f'<div class="code-header">'
        f'<span class="code-lang">{display_title}</span>'
        f'<button class="copy-btn" onclick="copyCode(this)" title="Copy code snippet">Copy</button>'
        f'</div>'
    )
    escaped_code = code.strip().replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return f'<div class="code-block">{title_bar}<pre><code class="language-{lang}">{escaped_code}</code></pre></div>'

def make_callout(kind, title, body):
    labels = {
        "note": "Note",
        "tip": "Tip",
        "warn": "Warning",
        "ban": "Compiler rule",
        "spec": "Specification"
    }
    label = title if title else labels.get(kind, "Note")
    return (
        f'<div class="callout callout-{kind}">'
        f'<div class="callout-title">{label}</div>'
        f'<div class="callout-body">{body}</div>'
        f'</div>'
    )

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Rook Documentation &amp; Programming Foundations (v0.5.0)</title>
<style>
  :root {
    --bg: #ffffff;
    --sidebar-bg: #f8fafc;
    --surface: #ffffff;
    --surface-secondary: #f1f5f9;
    --border: #e2e8f0;
    --border-strong: #cbd5e1;
    --text: #0f172a;
    --text-muted: #334155;
    --text-dim: #64748b;
    --primary: #2563eb;
    --primary-hover: #1d4ed8;
    --primary-dim: rgba(37, 99, 235, 0.08);

    /* Code block colors */
    --code-bg: #0f172a;
    --code-header: #090d16;
    --code-border: #1e293b;
    --code-text: #f1f5f9;

    /* Layout */
    --sidebar-width: 290px;
    --content-max-width: 860px;
    --nav-active-bg: rgba(37, 99, 235, 0.08);
    --nav-active-text: #1d4ed8;

    /* Syntax Tokens */
    --tok-cmt: #64748b;
    --tok-str: #86efac;
    --tok-kw: #c084fc;
    --tok-type: #38bdf8;
    --tok-prep: #fb923c;
    --tok-num: #fde047;
    --tok-fn: #93c5fd;
    --tok-bool: #f472b6;
    --tok-hdr: #38bdf8;
    --tok-key: #e2e8f0;
    --tok-cmd: #38bdf8;
    --tok-opt: #94a3b8;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  html {
    scroll-behavior: smooth;
    font-size: 16px;
  }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Inter, Helvetica, Arial, sans-serif;
    line-height: 1.7;
    display: flex;
    min-height: 100vh;
  }

  /* Sidebar */
  #sidebar {
    width: var(--sidebar-width);
    height: 100vh;
    position: sticky;
    top: 0;
    background: var(--sidebar-bg);
    border-right: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    z-index: 100;
    flex-shrink: 0;
  }

  .sidebar-header {
    padding: 18px 18px 14px;
    border-bottom: 1px solid var(--border);
  }

  .sidebar-title {
    font-size: 1.15rem;
    font-weight: 700;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;
    letter-spacing: -0.01em;
  }

  .sidebar-badge {
    font-size: 0.72rem;
    font-weight: 600;
    background: var(--primary);
    color: #ffffff;
    padding: 2px 7px;
    border-radius: 9999px;
  }

  .sidebar-sub {
    font-size: 0.8rem;
    color: var(--text-dim);
    margin-top: 3px;
  }

  /* Mode Switcher */
  .view-switcher-wrap {
    padding: 12px 14px;
    border-bottom: 1px solid var(--border);
  }

  .view-switcher {
    display: flex;
    background: #f1f5f9;
    padding: 3px;
    border-radius: 7px;
    gap: 2px;
    border: 1px solid var(--border);
  }

  .mode-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 7px 10px;
    font-size: 0.8rem;
    font-weight: 600;
    border: none;
    background: transparent;
    color: var(--text-dim);
    border-radius: 5px;
    cursor: pointer;
    transition: all 0.12s ease;
  }

  .mode-btn:hover {
    color: var(--text);
  }

  .mode-btn.active {
    background: #ffffff;
    color: var(--primary);
    box-shadow: 0 1px 2px rgba(0,0,0,0.06);
  }

  /* Search */
  .sidebar-search {
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
  }

  .search-wrap {
    position: relative;
    display: flex;
    align-items: center;
  }

  .search-wrap input {
    width: 100%;
    padding: 7px 32px 7px 10px;
    border-radius: 6px;
    border: 1px solid var(--border);
    background: #ffffff;
    font-size: 0.82rem;
    color: var(--text);
    outline: none;
    transition: border-color 0.15s ease;
  }

  .search-wrap input:focus {
    border-color: var(--primary);
  }

  .search-kbd {
    position: absolute;
    right: 8px;
    font-size: 0.7rem;
    font-family: ui-monospace, monospace;
    color: var(--text-dim);
    background: var(--surface-secondary);
    border: 1px solid var(--border);
    border-radius: 3px;
    padding: 1px 5px;
    pointer-events: none;
  }

  /* Navigation Links */
  .nav-list {
    flex: 1;
    overflow-y: auto;
    padding: 10px 8px 40px;
    display: flex;
    flex-direction: column;
    gap: 1px;
  }

  .nav-item {
    display: block;
    padding: 7px 10px;
    border-radius: 5px;
    color: var(--text-muted);
    text-decoration: none;
    font-size: 0.84rem;
    line-height: 1.35;
    border-left: 2px solid transparent;
    transition: all 0.12s ease;
  }

  .nav-item:hover {
    background: var(--surface-secondary);
    color: var(--text);
  }

  .nav-item.active {
    background: var(--nav-active-bg);
    color: var(--nav-active-text);
    border-left-color: var(--primary);
    font-weight: 600;
  }

  /* Main Container */
  #main {
    flex: 1;
    min-width: 0;
    padding: 40px 60px 120px;
    max-width: calc(var(--content-max-width) + 120px);
    margin: 0 auto;
  }

  /* Hero Section */
  .hero {
    padding: 10px 0 32px;
    margin-bottom: 24px;
    border-bottom: 1px solid var(--border);
  }

  .hero h1 {
    font-size: 2.1rem;
    font-weight: 800;
    color: var(--text);
    letter-spacing: -0.025em;
    margin-bottom: 10px;
  }

  .hero-tagline {
    font-size: 1.05rem;
    color: var(--text-muted);
    line-height: 1.6;
    margin-bottom: 18px;
    max-width: 72ch;
  }

  .hero-meta {
    font-size: 0.82rem;
    color: var(--text-dim);
    display: flex;
    flex-wrap: wrap;
    gap: 8px 16px;
    align-items: center;
    background: var(--surface-secondary);
    padding: 10px 14px;
    border-radius: 6px;
    border: 1px solid var(--border);
  }

  /* Chapter Section */
  .chapter {
    padding-top: 40px;
    margin-bottom: 48px;
    border-top: 1px solid var(--border);
  }

  .chapter:first-of-type {
    border-top: none;
    padding-top: 8px;
  }

  .chapter-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 20px;
  }

  .chapter-title {
    font-size: 1.6rem;
    font-weight: 700;
    color: var(--text);
    letter-spacing: -0.02em;
    line-height: 1.3;
  }

  .back-to-top {
    font-size: 0.8rem;
    color: var(--text-dim);
    text-decoration: none;
    font-weight: 500;
    padding: 3px 8px;
    border-radius: 4px;
    transition: all 0.15s ease;
  }

  .back-to-top:hover {
    color: var(--primary);
    background: var(--surface-secondary);
  }

  .chapter-body h3 {
    font-size: 1.2rem;
    font-weight: 600;
    margin: 34px 0 12px;
    color: var(--text);
    letter-spacing: -0.01em;
  }

  .chapter-body h4 {
    font-size: 1.02rem;
    font-weight: 600;
    margin: 24px 0 8px;
    color: #1e293b;
  }

  .chapter-body p {
    margin-bottom: 16px;
    color: var(--text-muted);
    font-size: 0.96rem;
    line-height: 1.7;
  }

  .chapter-body ul, .chapter-body ol {
    margin: 0 0 20px 22px;
    color: var(--text-muted);
    font-size: 0.96rem;
    line-height: 1.65;
  }

  .chapter-body li {
    margin-bottom: 6px;
  }

  /* Code Blocks */
  .code-block {
    margin: 16px 0 22px;
    border-radius: 6px;
    overflow: hidden;
    background: var(--code-bg);
    border: 1px solid var(--code-border);
  }

  .code-header {
    background: var(--code-header);
    padding: 6px 14px;
    border-bottom: 1px solid var(--code-border);
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .code-lang {
    font-family: ui-monospace, "JetBrains Mono", Menlo, monospace;
    font-size: 0.72rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #94a3b8;
    letter-spacing: 0.05em;
  }

  .copy-btn {
    background: transparent;
    border: 1px solid #334155;
    border-radius: 4px;
    color: #94a3b8;
    font-size: 0.72rem;
    font-family: inherit;
    padding: 2px 8px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .copy-btn:hover {
    color: #f8fafc;
    border-color: #64748b;
    background: rgba(255, 255, 255, 0.06);
  }

  .copy-btn.copied {
    color: #4ade80;
    border-color: #22c55e;
  }

  .code-block pre {
    padding: 14px 16px;
    overflow-x: auto;
    font-family: ui-monospace, "JetBrains Mono", Menlo, Consolas, monospace;
    font-size: 0.88rem;
    line-height: 1.6;
    color: var(--code-text);
  }

  .code-block pre code {
    background: transparent;
    border: none;
    padding: 0;
    color: inherit;
    font-size: inherit;
  }

  /* Syntax Highlighting Tokens */
  .tok-cmt  { color: var(--tok-cmt); font-style: italic; }
  .tok-str  { color: var(--tok-str); }
  .tok-kw   { color: var(--tok-kw); font-weight: 600; }
  .tok-type { color: var(--tok-type); }
  .tok-prep { color: var(--tok-prep); }
  .tok-num  { color: var(--tok-num); }
  .tok-fn   { color: var(--tok-fn); }
  .tok-bool { color: var(--tok-bool); font-weight: 600; }
  .tok-hdr  { color: var(--tok-hdr); font-weight: 600; }
  .tok-key  { color: var(--tok-key); }
  .tok-cmd  { color: var(--tok-cmd); font-weight: 600; }
  .tok-opt  { color: var(--tok-opt); }

  /* Inline Code */
  code:not(pre code) {
    background: #f1f5f9;
    border: 1px solid #e2e8f0;
    color: #0f172a;
    padding: 2px 6px;
    border-radius: 4px;
    font-family: ui-monospace, SFMono-Regular, "JetBrains Mono", Menlo, monospace;
    font-size: 0.86em;
    font-weight: 500;
  }

  /* Callout Boxes */
  .callout {
    border-radius: 6px;
    padding: 16px 18px;
    margin: 20px 0;
    border: 1px solid var(--border);
    border-left: 3px solid var(--border-strong);
    background: #f8fafc;
  }

  .callout-title {
    font-weight: 700;
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    margin-bottom: 6px;
  }

  .callout-body {
    font-size: 0.92rem;
    line-height: 1.6;
    color: var(--text-muted);
  }

  .callout-body p:last-child { margin-bottom: 0; }

  .callout-body pre {
    margin-top: 10px;
    background: #0f172a;
    color: #f1f5f9;
    padding: 10px 12px;
    border-radius: 5px;
    overflow-x: auto;
    font-size: 0.84rem;
  }

  /* Unified callout variants */
  .callout-note, .callout-tip {
    border-left-color: #2563eb;
  }
  .callout-note .callout-title, .callout-tip .callout-title {
    color: #2563eb;
  }

  .callout-warn {
    border-left-color: #d97706;
  }
  .callout-warn .callout-title {
    color: #b45309;
  }

  .callout-ban {
    border-left-color: #e11d48;
  }
  .callout-ban .callout-title {
    color: #be123c;
  }

  .callout-spec {
    border-left-color: #64748b;
  }
  .callout-spec .callout-title {
    color: #475569;
  }

  /* Tables */
  .table-container {
    border: 1px solid var(--border);
    border-radius: 7px;
    overflow-x: auto;
    margin: 20px 0;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.9rem;
    text-align: left;
    background: #ffffff;
  }

  th {
    background: #f8fafc;
    color: #475569;
    font-weight: 600;
    font-size: 0.8rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
  }

  td {
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
    color: var(--text-muted);
    vertical-align: top;
  }

  tr:last-child td {
    border-bottom: none;
  }

  tr:hover td {
    background: #f8fafc;
  }

  /* Diagram styles */
  .arch-diagram {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 12px;
    padding: 20px 24px;
    background: #f8fafc;
    border: 1px solid var(--border);
    border-radius: 8px;
    margin: 20px 0;
    flex-wrap: wrap;
  }

  .arch-box {
    background: #ffffff;
    border: 1px solid var(--border-strong);
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.85rem;
    line-height: 1.45;
    color: var(--text);
    text-align: center;
    box-shadow: 0 1px 2px rgba(0,0,0,0.03);
  }

  .arch-split {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .arch-box.branch {
    font-size: 0.8rem;
    font-weight: 500;
    padding: 8px 12px;
  }

  .arch-arrow {
    font-size: 1.1rem;
    color: var(--text-dim);
    user-select: none;
  }

  /* Mobile Responsiveness */
  @media (max-width: 960px) {
    body {
      flex-direction: column;
    }
    #sidebar {
      width: 100%;
      height: auto;
      position: relative;
      border-right: none;
      border-bottom: 1px solid var(--border);
    }
    .nav-list {
      max-height: 220px;
    }
    #main {
      padding: 24px 20px 80px;
    }
    .chapter {
      padding-top: 28px;
      margin-bottom: 36px;
    }
  }
</style>
</head>
<body>

<aside id="sidebar">
  <div class="sidebar-header">
    <div class="sidebar-title">
      <span>Rook Documentation</span>
      <span class="sidebar-badge">v0.5.0</span>
    </div>
    <div class="sidebar-sub">Systems Programming &amp; Foundations</div>
  </div>

  <div class="view-switcher-wrap">
    <div class="view-switcher">
      <button id="btn-mode-guide" class="mode-btn active" onclick="switchMode('guide')">
        <span class="mode-text">Language Guide</span>
      </button>
      <button id="btn-mode-beginner" class="mode-btn" onclick="switchMode('beginner')">
        <span class="mode-text">Beginner Course</span>
      </button>
    </div>
  </div>

  <div class="sidebar-search">
    <div class="search-wrap">
      <input type="text" id="filter-input" placeholder="Filter chapters..." onkeyup="filterChapters()">
      <span class="search-kbd">/</span>
    </div>
  </div>

  <nav class="nav-list" id="nav-guide">
    <!--GUIDE_NAV-->
  </nav>

  <nav class="nav-list" id="nav-beginner" style="display:none;">
    <!--BEGINNER_NAV-->
  </nav>
</aside>

<main id="main">
  <!-- VIEW 1: LANGUAGE GUIDE -->
  <div id="view-guide" class="view-container">
    <header class="hero" id="top-guide">
      <h1>The Rook Language Guide</h1>
      <p class="hero-tagline">
        A technical reference manual for Rook, covering syntax, memory semantics, C ABI compatibility, single inheritance, and compiler toolchains.
      </p>
      <div class="hero-meta">
        <span>Compiler: <code>rokade</code></span>
        <span>Target audience: Developers with programming experience</span>
        <span>Offline documentation</span>
      </div>
    </header>

    <!--GUIDE_BODY-->
  </div>

  <!-- VIEW 2: BEGINNER COURSE -->
  <div id="view-beginner" class="view-container" style="display:none;">
    <header class="hero" id="top-beginner">
      <h1>Programming Foundations: From Scratch</h1>
      <p class="hero-tagline">
        A step-by-step introduction to programming fundamentals: how computers store data, execute instructions, manage memory, and structure logic.
      </p>
      <div class="hero-meta">
        <span>Audience: New learners</span>
        <span>Focus: Practical concepts and clear explanations</span>
        <span>Offline course</span>
      </div>
    </header>

    <!--BEGINNER_BODY-->
  </div>

  <footer style="margin-top: 60px; padding: 24px 0; border-top: 1px solid var(--border); color: var(--text-dim); font-size: 0.85rem; text-align: center;">
    <p>Rook Language Guide and Programming Foundations &bull; Version 0.5.0 &bull; Offline documentation</p>
  </footer>
</main>

<script>
  // Mode switcher: 'guide' vs 'beginner'
  function switchMode(mode) {
    var btnGuide = document.getElementById('btn-mode-guide');
    var btnBeginner = document.getElementById('btn-mode-beginner');
    var navGuide = document.getElementById('nav-guide');
    var navBeginner = document.getElementById('nav-beginner');
    var viewGuide = document.getElementById('view-guide');
    var viewBeginner = document.getElementById('view-beginner');
    var filterInput = document.getElementById('filter-input');

    if (mode === 'beginner') {
      btnGuide.classList.remove('active');
      btnBeginner.classList.add('active');
      navGuide.style.display = 'none';
      navBeginner.style.display = 'flex';
      viewGuide.style.display = 'none';
      viewBeginner.style.display = 'block';
      filterInput.placeholder = 'Filter beginner modules...';
    } else {
      btnBeginner.classList.remove('active');
      btnGuide.classList.add('active');
      navBeginner.style.display = 'none';
      navGuide.style.display = 'flex';
      viewBeginner.style.display = 'none';
      viewGuide.style.display = 'block';
      filterInput.placeholder = 'Filter guide chapters...';
    }
    filterInput.value = '';
    filterChapters();
  }

  // Filter visible sidebar navigation items
  function filterChapters() {
    var input = document.getElementById('filter-input');
    var filter = input.value.toLowerCase();
    var activeNav = document.getElementById('nav-beginner').style.display !== 'none' 
      ? document.getElementById('nav-beginner') 
      : document.getElementById('nav-guide');
    var items = activeNav.getElementsByClassName('nav-item');
    for (var i = 0; i < items.length; i++) {
      var text = items[i].textContent || items[i].innerText;
      if (text.toLowerCase().indexOf(filter) > -1) {
        items[i].style.display = "";
      } else {
        items[i].style.display = "none";
      }
    }
  }

  // Pure JavaScript Client-Side Syntax Highlighter (100% Offline)
  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function highlightCAndRook(src) {
    var masterRegex = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*')|(^\s*#(?:include|comprise|define|raw_c|end_raw_c|ifdef|ifndef|endif|comprise_lib)[^\n]*)|(\b(?:let|fn|struct|object|impl|sum|enum|match|defer|return|if|else|while|for|in|switch|case|default|break|continue|extern|auto|typedef|sizeof|const|inline|static)\b)|(\b(?:int|uint|float|double|i8|u8|i16|u16|i32|u32|i64|u64|f32|f64|bool|void|char|string|size_t|ssize_t|int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|FILE|Vector2|Point|Entity|Shape|Node|Rectangle|Color|Circle)\b)|(\b(?:true|false|NULL|null)\b)|(\b0x[0-9a-fA-F]+\b|\b\d+(?:\.\d+)?(?:f|u|U|L|LL|ll)?\b)|(\b[a-zA-Z_]\w*(?=\s*\())/gm;
    var lastIndex = 0;
    var out = "";
    var match;
    while ((match = masterRegex.exec(src)) !== null) {
      if (match.index > lastIndex) {
        out += escapeHtml(src.substring(lastIndex, match.index));
      }
      var full = match[0];
      var cmt = match[1], str = match[2], prep = match[3], kw = match[4], type = match[5], boolean = match[6], num = match[7], fn = match[8];
      if (cmt) out += '<span class="tok-cmt">' + escapeHtml(cmt) + '</span>';
      else if (str) out += '<span class="tok-str">' + escapeHtml(str) + '</span>';
      else if (prep) out += '<span class="tok-prep">' + escapeHtml(prep) + '</span>';
      else if (kw) out += '<span class="tok-kw">' + escapeHtml(kw) + '</span>';
      else if (type) out += '<span class="tok-type">' + escapeHtml(type) + '</span>';
      else if (boolean) out += '<span class="tok-bool">' + escapeHtml(boolean) + '</span>';
      else if (num) out += '<span class="tok-num">' + escapeHtml(num) + '</span>';
      else if (fn) out += '<span class="tok-fn">' + escapeHtml(fn) + '</span>';
      lastIndex = masterRegex.lastIndex;
    }
    if (lastIndex < src.length) {
      out += escapeHtml(src.substring(lastIndex));
    }
    return out;
  }

  function highlightToml(src) {
    var masterRegex = /(#[^\n]*)|(^\s*\[[^\]]+\])|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(^\s*[\w.-]+(?=\s*=))|(\b(?:true|false)\b)|(\b\d+\b)/gm;
    var lastIndex = 0;
    var out = "";
    var match;
    while ((match = masterRegex.exec(src)) !== null) {
      if (match.index > lastIndex) {
        out += escapeHtml(src.substring(lastIndex, match.index));
      }
      var cmt = match[1], hdr = match[2], str = match[3], key = match[4], boolean = match[5], num = match[6];
      if (cmt) out += '<span class="tok-cmt">' + escapeHtml(cmt) + '</span>';
      else if (hdr) out += '<span class="tok-hdr">' + escapeHtml(hdr) + '</span>';
      else if (str) out += '<span class="tok-str">' + escapeHtml(str) + '</span>';
      else if (key) out += '<span class="tok-key">' + escapeHtml(key) + '</span>';
      else if (boolean) out += '<span class="tok-bool">' + escapeHtml(boolean) + '</span>';
      else if (num) out += '<span class="tok-num">' + escapeHtml(num) + '</span>';
      lastIndex = masterRegex.lastIndex;
    }
    if (lastIndex < src.length) {
      out += escapeHtml(src.substring(lastIndex));
    }
    return out;
  }

  function highlightBash(src) {
    var masterRegex = /(#[^\n]*)|("(?:\\.|[^"\\])*"|'[^']*')|(\b(?:rokade|git|cmake|ninja|cargo|gcc|clang|sudo|apt-get|export|cd|rm|bash)\b)|(--?[\w-]+)/gm;
    var lastIndex = 0;
    var out = "";
    var match;
    while ((match = masterRegex.exec(src)) !== null) {
      if (match.index > lastIndex) {
        out += escapeHtml(src.substring(lastIndex, match.index));
      }
      var cmt = match[1], str = match[2], cmd = match[3], opt = match[4];
      if (cmt) out += '<span class="tok-cmt">' + escapeHtml(cmt) + '</span>';
      else if (str) out += '<span class="tok-str">' + escapeHtml(str) + '</span>';
      else if (cmd) out += '<span class="tok-cmd">' + escapeHtml(cmd) + '</span>';
      else if (opt) out += '<span class="tok-opt">' + escapeHtml(opt) + '</span>';
      lastIndex = masterRegex.lastIndex;
    }
    if (lastIndex < src.length) {
      out += escapeHtml(src.substring(lastIndex));
    }
    return out;
  }

  function highlightAllCodeBlocks() {
    var blocks = document.querySelectorAll('pre code[class*="language-"]');
    for (var i = 0; i < blocks.length; i++) {
      var codeEl = blocks[i];
      var raw = codeEl.textContent;
      var className = codeEl.className;
      if (className.indexOf('language-rook') > -1 || className.indexOf('language-c') > -1) {
        codeEl.innerHTML = highlightCAndRook(raw);
      } else if (className.indexOf('language-toml') > -1) {
        codeEl.innerHTML = highlightToml(raw);
      } else if (className.indexOf('language-bash') > -1 || className.indexOf('language-powershell') > -1) {
        codeEl.innerHTML = highlightBash(raw);
      }
    }
  }

  // Copy code snippet to clipboard
  function copyCode(btn) {
    var block = btn.closest('.code-block');
    if (!block) return;
    var codeEl = block.querySelector('pre code');
    if (!codeEl) return;
    var text = codeEl.textContent;
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(function() {
        showCopied(btn);
      }).catch(function() {
        fallbackCopy(text, btn);
      });
    } else {
      fallbackCopy(text, btn);
    }
  }

  function fallbackCopy(text, btn) {
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    try {
      document.execCommand('copy');
      showCopied(btn);
    } catch (e) {}
    document.body.removeChild(ta);
  }

  function showCopied(btn) {
    var orig = btn.textContent;
    btn.textContent = 'Copied!';
    btn.classList.add('copied');
    setTimeout(function() {
      btn.textContent = orig;
      btn.classList.remove('copied');
    }, 1800);
  }

  // Keyboard shortcut '/' to focus search
  window.addEventListener('keydown', function(e) {
    if (e.key === '/' && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'TEXTAREA') {
      e.preventDefault();
      var input = document.getElementById('filter-input');
      if (input) {
        input.focus();
        input.select();
      }
    }
  });

  // ScrollSpy to highlight active chapter in current view
  function setupScrollSpy() {
    function onScroll() {
      var scrollPos = window.scrollY + 100;
      var activeNavId = document.getElementById('nav-beginner').style.display !== 'none' ? 'nav-beginner' : 'nav-guide';
      var activeViewId = activeNavId === 'nav-beginner' ? 'view-beginner' : 'view-guide';
      var sections = document.querySelectorAll('#' + activeViewId + ' .chapter');
      var navLinks = document.querySelectorAll('#' + activeNavId + ' .nav-item');

      sections.forEach(function(sec) {
        var top = sec.offsetTop;
        var height = sec.offsetHeight;
        var id = sec.getAttribute('id');
        if (scrollPos >= top && scrollPos < top + height) {
          navLinks.forEach(function(link) {
            link.classList.remove('active');
            if (link.getAttribute('href') === '#' + id) {
              link.classList.add('active');
            }
          });
        }
      });
    }
    window.addEventListener('scroll', onScroll);
    onScroll();
  }

  // Route URL hash on load or hash change
  function handleHashRoute() {
    var hash = window.location.hash;
    if (hash) {
      var id = hash.substring(1);
      if (id.indexOf('b') === 0 || id.indexOf('beginner') === 0) {
        switchMode('beginner');
      } else {
        switchMode('guide');
      }
      var target = document.getElementById(id);
      if (target) {
        setTimeout(function() { target.scrollIntoView(); }, 50);
      }
    }
  }

  window.addEventListener('DOMContentLoaded', function() {
    highlightAllCodeBlocks();
    setupScrollSpy();
    handleHashRoute();
  });
  window.addEventListener('hashchange', handleHashRoute);
</script>

</body>
</html>
"""

def generate_html():
    guide_chapters = get_guide_chapters(make_code_box, make_callout)
    beginner_modules = get_beginner_modules(make_code_box, make_callout)

    # Build sidebar links for Guide
    guide_links = []
    for cid, title, _ in guide_chapters:
        guide_links.append(f'<a href="#{cid}" class="nav-item nav-guide-item" data-id="{cid}">{title}</a>')
    guide_nav_html = "\n    ".join(guide_links)

    # Build sidebar links for Beginner
    beginner_links = []
    for mid, title, _ in beginner_modules:
        beginner_links.append(f'<a href="#{mid}" class="nav-item nav-beginner-item" data-id="{mid}">{title}</a>')
    beginner_nav_html = "\n    ".join(beginner_links)

    # Build Guide body content
    guide_sections = []
    for cid, title, content in guide_chapters:
        sec = f"""
<section class="chapter" id="{cid}">
  <div class="chapter-header">
    <h2 class="chapter-title">{title}</h2>
    <a href="#top-guide" class="back-to-top">Top ↑</a>
  </div>
  <div class="chapter-body">
    {content}
  </div>
</section>
"""
        guide_sections.append(sec)
    guide_body_html = "\n".join(guide_sections)

    # Build Beginner body content
    beginner_sections = []
    for mid, title, content in beginner_modules:
        sec = f"""
<section class="chapter beginner-chapter" id="{mid}">
  <div class="chapter-header">
    <h2 class="chapter-title">{title}</h2>
    <a href="#top-beginner" class="back-to-top">Top ↑</a>
  </div>
  <div class="chapter-body">
    {content}
  </div>
</section>
"""
        beginner_sections.append(sec)
    beginner_body_html = "\n".join(beginner_sections)

    html = HTML_TEMPLATE.replace("<!--GUIDE_NAV-->", guide_nav_html)
    html = html.replace("<!--BEGINNER_NAV-->", beginner_nav_html)
    html = html.replace("<!--GUIDE_BODY-->", guide_body_html)
    html = html.replace("<!--BEGINNER_BODY-->", beginner_body_html)
    return html

def main():
    print(f"Generating documentation for Rook & Rokade v0.5.0 at {OUTPUT_FILE}...")
    html_content = generate_html()
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(html_content)
    print(f"Successfully generated {OUTPUT_FILE} ({len(html_content)} bytes).")

if __name__ == "__main__":
    main()
