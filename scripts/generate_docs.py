#!/usr/bin/env python3
"""
generate_docs.py
Generates the comprehensive, modernized, dual-mode Rook Language Guide & Beginner Course
(docs/rook-language-guide.html) for Rook & Rokade v0.4.2.
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
    title_bar = f'<div class="code-header"><span class="code-lang">{title or lang}</span></div>' if (title or lang) else ''
    escaped_code = code.strip().replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return f'<div class="code-block">{title_bar}<pre><code class="language-{lang}">{escaped_code}</code></pre></div>'

def make_callout(kind, title, body):
    icons = {
        "note": "ℹ️ Note",
        "tip": "💡 Best Practice / Insight",
        "warn": "⚠️ Critical Warning & Gotcha",
        "ban": "🚫 Compile-Time Enforcement",
        "spec": "📐 Specification & Design"
    }
    label = title if title else icons.get(kind, "Information")
    return f'<div class="callout callout-{kind}"><div class="callout-title">{label}</div><div class="callout-body">{body}</div></div>'

HTML_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Rook Language Guide &amp; Programming Foundations (v0.4.2)</title>
<style>
  :root {
    --bg: #f8fafc;
    --surface: #ffffff;
    --surface-secondary: #f1f5f9;
    --border: #e2e8f0;
    --border-strong: #cbd5e1;
    --text: #0f172a;
    --text-muted: #475569;
    --text-dim: #64748b;
    --primary: #2563eb;
    --primary-hover: #1d4ed8;
    --primary-dim: rgba(37, 99, 235, 0.08);
    --code-bg: #141824;
    --code-header: #0d101a;
    --code-text: #f8fafc;
    --sidebar-bg: #ffffff;
    --sidebar-width: 320px;
    --nav-active-bg: #eff6ff;
    --nav-active-text: #1d4ed8;
    
    /* Callout Colors */
    --note-bg: #f8fafc;
    --note-border: #2563eb;
    --note-title: #2563eb;
    --tip-bg: #ecfdf5;
    --tip-border: #10b981;
    --tip-title: #065f46;
    --warn-bg: #fffbeb;
    --warn-border: #f59e0b;
    --warn-title: #92400e;
    --ban-bg: #fff1f2;
    --ban-border: #f43f5e;
    --ban-title: #9f1239;
    --spec-bg: #f8fafc;
    --spec-border: #64748b;
    --spec-title: #334155;

    /* Syntax Highlighting Tokens */
    --tok-cmt: #8592a6;
    --tok-str: #4ade80;
    --tok-kw: #c084fc;
    --tok-type: #38bdf8;
    --tok-prep: #fb923c;
    --tok-num: #facc15;
    --tok-fn: #818cf8;
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
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Inter", Helvetica, Arial, sans-serif;
    line-height: 1.7;
    display: flex;
    min-height: 100vh;
  }

  /* Layout */
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
    padding: 20px 20px 14px;
    border-bottom: 1px solid var(--border);
  }

  .sidebar-title {
    font-size: 1.25rem;
    font-weight: 700;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .sidebar-badge {
    font-size: 0.75rem;
    font-weight: 600;
    background: var(--primary);
    color: #fff;
    padding: 2px 8px;
    border-radius: 9999px;
  }

  .sidebar-sub {
    font-size: 0.85rem;
    color: var(--text-dim);
    margin-top: 4px;
  }

  /* View Switcher */
  .view-switcher-wrap {
    padding: 12px 16px;
    border-bottom: 1px solid var(--border);
    background: #fafafa;
  }

  .view-switcher {
    display: flex;
    background: var(--surface-secondary);
    padding: 4px;
    border-radius: 8px;
    gap: 4px;
    border: 1px solid var(--border-strong);
  }

  .mode-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
    padding: 8px 10px;
    font-size: 0.82rem;
    font-weight: 600;
    border: none;
    background: transparent;
    color: var(--text-dim);
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .mode-btn:hover {
    color: var(--text);
  }

  .mode-btn.active {
    background: var(--surface);
    color: var(--primary);
    box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  }

  .sidebar-search {
    padding: 10px 16px;
    border-bottom: 1px solid var(--border);
  }

  .sidebar-search input {
    width: 100%;
    padding: 8px 12px;
    border-radius: 6px;
    border: 1px solid var(--border-strong);
    background: var(--surface-secondary);
    font-size: 0.85rem;
    color: var(--text);
    outline: none;
    transition: border-color 0.15s ease;
  }

  .sidebar-search input:focus {
    border-color: var(--primary);
    background: var(--surface);
  }

  .nav-list {
    flex: 1;
    overflow-y: auto;
    padding: 12px 10px 40px;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nav-item {
    display: block;
    padding: 8px 12px;
    border-radius: 6px;
    color: var(--text-muted);
    text-decoration: none;
    font-size: 0.88rem;
    line-height: 1.4;
    transition: all 0.15s ease;
  }

  .nav-item:hover {
    background: var(--surface-secondary);
    color: var(--text);
  }

  .nav-item.active {
    background: var(--nav-active-bg);
    color: var(--nav-active-text);
    font-weight: 600;
  }

  #main {
    flex: 1;
    min-width: 0;
    padding: 40px 48px 120px;
    max-width: 1020px;
    margin: 0 auto;
  }

  /* Hero Banner */
  .hero {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 40px;
    margin-bottom: 48px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.04);
  }

  .hero h1 {
    font-size: 2.3rem;
    font-weight: 800;
    color: var(--text);
    letter-spacing: -0.025em;
    margin-bottom: 12px;
  }

  .hero-tagline {
    font-size: 1.12rem;
    color: var(--text-muted);
    margin-bottom: 24px;
    max-width: 75ch;
  }

  .badges {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 24px;
  }

  .badge {
    font-size: 0.8rem;
    font-weight: 600;
    padding: 4px 10px;
    border-radius: 6px;
    background: var(--surface-secondary);
    border: 1px solid var(--border-strong);
    color: var(--text-muted);
  }

  .badge.highlight {
    background: #dbeafe;
    border-color: #93c5fd;
    color: #1e40af;
  }

  .badge.pedagogy {
    background: #ecfdf5;
    border-color: #6ee7b7;
    color: #065f46;
  }

  .hero-meta {
    font-size: 0.9rem;
    color: var(--text-dim);
    border-top: 1px solid var(--border);
    padding-top: 16px;
  }

  /* Chapter Card Styling */
  .chapter {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 36px 40px;
    margin-bottom: 40px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.03);
  }

  .chapter-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    border-bottom: 2px solid var(--border);
    padding-bottom: 16px;
    margin-bottom: 24px;
  }

  .chapter-title {
    font-size: 1.65rem;
    font-weight: 700;
    color: var(--text);
    letter-spacing: -0.015em;
  }

  .back-to-top {
    font-size: 0.85rem;
    color: var(--text-dim);
    text-decoration: none;
    font-weight: 500;
  }

  .back-to-top:hover {
    color: var(--primary);
  }

  .chapter-body h3 {
    font-size: 1.25rem;
    font-weight: 600;
    margin: 32px 0 14px;
    color: var(--text);
  }

  .chapter-body h4 {
    font-size: 1.05rem;
    font-weight: 600;
    margin: 24px 0 10px;
    color: var(--text);
  }

  .chapter-body p {
    margin-bottom: 16px;
    color: var(--text);
  }

  .chapter-body ul, .chapter-body ol {
    margin: 0 0 20px 24px;
    color: var(--text);
  }

  .chapter-body li {
    margin-bottom: 8px;
  }

  /* Code Blocks */
  .code-block {
    margin: 18px 0 24px;
    border-radius: 8px;
    overflow: hidden;
    background: var(--code-bg);
    border: 1px solid #232938;
    box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
  }

  .code-header {
    background: var(--code-header);
    padding: 8px 16px;
    border-bottom: 1px solid #1f2533;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  .code-lang {
    font-family: ui-monospace, SFMono-Regular, "JetBrains Mono", monospace;
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #94a3b8;
    letter-spacing: 0.05em;
  }

  .code-block pre {
    padding: 16px 20px;
    overflow-x: auto;
    font-family: ui-monospace, SFMono-Regular, "JetBrains Mono", Menlo, Consolas, monospace;
    font-size: 0.9rem;
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

  /* Syntax Highlighting Token Styles */
  .tok-cmt  { color: var(--tok-cmt); font-style: italic; }
  .tok-str  { color: var(--tok-str); }
  .tok-kw   { color: var(--tok-kw); font-weight: 600; }
  .tok-type { color: var(--tok-type); font-weight: 500; }
  .tok-prep { color: var(--tok-prep); }
  .tok-num  { color: var(--tok-num); }
  .tok-fn   { color: var(--tok-fn); }
  .tok-bool { color: var(--tok-bool); font-weight: 600; }
  .tok-hdr  { color: var(--tok-hdr); font-weight: 600; }
  .tok-key  { color: var(--tok-key); font-weight: 500; }
  .tok-cmd  { color: var(--tok-cmd); font-weight: 600; }
  .tok-opt  { color: var(--tok-opt); }

  /* Inline Code */
  code:not(pre code) {
    background: var(--surface-secondary);
    border: 1px solid var(--border);
    padding: 2px 6px;
    border-radius: 4px;
    font-family: ui-monospace, SFMono-Regular, "JetBrains Mono", monospace;
    font-size: 0.88em;
    color: #b91c1c;
  }

  /* Callout Boxes */
  .callout {
    border-radius: 8px;
    padding: 18px 20px;
    margin: 20px 0 24px;
    border: 1px solid var(--border);
    border-left: 4px solid transparent;
  }

  .callout-title {
    font-weight: 700;
    font-size: 0.95rem;
    margin-bottom: 6px;
  }

  .callout-body {
    font-size: 0.95rem;
    line-height: 1.6;
  }

  .callout-body p:last-child { margin-bottom: 0; }

  .callout-note { background: var(--note-bg); border-color: var(--note-border); }
  .callout-note .callout-title { color: var(--note-title); }

  .callout-tip { background: var(--tip-bg); border-color: var(--tip-border); }
  .callout-tip .callout-title { color: var(--tip-title); }

  .callout-warn { background: var(--warn-bg); border-color: var(--warn-border); }
  .callout-warn .callout-title { color: var(--warn-title); }

  .callout-ban { background: var(--ban-bg); border-color: var(--ban-border); }
  .callout-ban .callout-title { color: var(--ban-title); }

  .callout-spec { background: var(--spec-bg); border-color: var(--spec-border); }
  .callout-spec .callout-title { color: var(--spec-title); }

  /* Tables */
  .table-container {
    overflow-x: auto;
    margin: 20px 0;
    border-radius: 8px;
    border: 1px solid var(--border);
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.92rem;
    text-align: left;
    background: var(--surface);
  }

  th {
    background: var(--surface-secondary);
    color: var(--text);
    font-weight: 600;
    padding: 12px 16px;
    border-bottom: 1px solid var(--border);
  }

  td {
    padding: 12px 16px;
    border-bottom: 1px solid var(--border);
    color: var(--text-muted);
    vertical-align: top;
  }

  tr:last-child td {
    border-bottom: none;
  }

  tr:hover td {
    background: #fafafa;
  }

  /* Diagram styles */
  .arch-diagram {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 16px;
    padding: 24px;
    background: var(--surface-secondary);
    border: 1px solid var(--border);
    border-radius: 8px;
    margin: 20px 0;
    flex-wrap: wrap;
  }

  .arch-box {
    background: var(--surface);
    border: 1px solid var(--border-strong);
    border-radius: 6px;
    padding: 12px 16px;
    font-size: 0.88rem;
    font-weight: 600;
    text-align: center;
    box-shadow: 0 1px 2px rgba(0,0,0,0.04);
  }

  .arch-split {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .arch-box.branch {
    font-size: 0.82rem;
    font-weight: 500;
    padding: 8px 12px;
  }

  .arch-arrow {
    font-size: 1.2rem;
    color: var(--text-dim);
  }

  /* Mobile Responsiveness */
  @media (max-width: 1024px) {
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
      max-height: 260px;
    }
    #main {
      padding: 24px 20px 80px;
    }
    .chapter {
      padding: 24px 20px;
    }
    .hero {
      padding: 28px 20px;
    }
  }
</style>
</head>
<body>

<aside id="sidebar">
  <div class="sidebar-header">
    <div class="sidebar-title">
      <span>Rook Guide</span>
      <span class="sidebar-badge">v0.4.2</span>
    </div>
    <div class="sidebar-sub">Systems Programming &amp; Foundations</div>
  </div>

  <div class="view-switcher-wrap">
    <div class="view-switcher">
      <button id="btn-mode-guide" class="mode-btn active" onclick="switchMode('guide')">
        <span class="mode-icon">📘</span>
        <span class="mode-text">Language Guide</span>
      </button>
      <button id="btn-mode-beginner" class="mode-btn" onclick="switchMode('beginner')">
        <span class="mode-icon">🎓</span>
        <span class="mode-text">Beginner Course</span>
      </button>
    </div>
  </div>

  <div class="sidebar-search">
    <input type="text" id="filter-input" placeholder="Filter guide chapters..." onkeyup="filterChapters()">
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
        A technical specification and comprehensive reference manual for <strong>Rook</strong>: a modern systems language with 1:1 C ABI compatibility, memory-safe defaults, zero-overhead single inheritance, and dual C/LLVM compilation backends.
      </p>
      <div class="hero-meta">
        Compiler: <code>rokade</code> &nbsp;|&nbsp; Target Audience: Developers with prior programming knowledge &nbsp;|&nbsp; Fully self-contained.
      </div>
    </header>

    <!--GUIDE_BODY-->
  </div>

  <!-- VIEW 2: BEGINNER COURSE -->
  <div id="view-beginner" class="view-container" style="display:none;">
    <header class="hero" id="top-beginner">
      <h1>Programming Foundations: From Scratch</h1>
      <p class="hero-tagline">
        A friendly, thorough, and step-by-step educational course designed for students and newcomers learning computer programming from the ground up. Master computational thinking, memory, data structures, and problem solving in plain English.
      </p>
      <div class="hero-meta">
        Audience: New enthusiast learners &nbsp;|&nbsp; Approach: Real-world analogies &amp; clean concepts &nbsp;|&nbsp; Fully self-contained.
      </div>
    </header>

    <!--BEGINNER_BODY-->
  </div>

  <footer style="margin-top: 60px; padding: 24px 0; border-top: 1px solid var(--border); color: var(--text-dim); font-size: 0.9rem; text-align: center;">
    <p>Rook Language Guide &amp; Programming Foundations &bull; Version 0.4.2 &bull; Built with Care &bull; Completely Offline</p>
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

  // ScrollSpy to highlight active chapter in current view
  function setupScrollSpy() {
    function onScroll() {
      var scrollPos = window.scrollY + 120;
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
    <a href="#top-guide" class="back-to-top">↑ Top</a>
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
    <a href="#top-beginner" class="back-to-top">↑ Top</a>
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
    print(f"Generating comprehensive guide for Rook & Rokade v0.4.2 at {OUTPUT_FILE}...")
    html_content = generate_html()
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(html_content)
    print(f"Successfully generated {OUTPUT_FILE} ({len(html_content)} bytes).")

if __name__ == "__main__":
    main()
