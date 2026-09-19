#!/usr/bin/env python3
"""
generate_docs.py
Generates the official, distraction-free Rook Documentation (The Rook Book)
in mdBook / Rust Book style for Rook & Rokade v0.7.0.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOK_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_FILE = os.path.join(ROOK_ROOT, "docs", "rook-language-guide.html")

sys.path.insert(0, SCRIPT_DIR)
from docs_guide import get_guide_chapters

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
        "ban": "Compiler Rule",
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
<html lang="en" class="warm">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>The Rook Programming Language (v0.7.0)</title>
<style>
  /* ─── mdBook Theme Variables ───────────────────────────────────────── */
  :root, html.warm {
    --bg: #f5ede1;
    --fg: #2e241f;
    --fg-muted: #6b584d;
    --sidebar-bg: #ecdcc9;
    --sidebar-fg: #2e241f;
    --sidebar-border: #d4bfab;
    --sidebar-active: #9b2c1b;
    --sidebar-active-bg: #e2cbb888;
    --menubar-bg: #f5ede1;
    --menubar-border: #d4bfab;
    --links: #9b2c1b;
    --links-hover: #6e1c10;
    --inline-code: #2e241f;
    --inline-code-bg: #fcf8f2;
    --inline-code-border: #d8c5b3;
    --code-bg: #25201d;
    --code-header: #1c1815;
    --code-border: #3d342f;
    --code-fg: #f5ede1;
    --table-border: #d4bfab;
    --table-header-bg: #ecdcc9;
    --table-row-alt: #f8f2e9;
    --quote-bg: #ece0d1;
    --quote-border: #9b2c1b;
    --heading-border: #d8c5b3;
    --nav-btn-bg: #ecdcc9;
    --nav-btn-border: #cbb49e;
    --nav-btn-fg: #2e241f;
  }

  html.coal {
    --bg: #141617;
    --fg: #c5bda7;
    --fg-muted: #888075;
    --sidebar-bg: #1d2021;
    --sidebar-fg: #c5bda7;
    --sidebar-border: #282828;
    --sidebar-active: #0480aa;
    --sidebar-active-bg: #0480aa22;
    --menubar-bg: #141617;
    --menubar-border: #282828;
    --links: #0480aa;
    --links-hover: #199bc4;
    --inline-code: #ebdbb2;
    --inline-code-bg: #282828;
    --inline-code-border: #3c3836;
    --code-bg: #1d2021;
    --code-header: #141617;
    --code-border: #3c3836;
    --code-fg: #ebdbb2;
    --table-border: #282828;
    --table-header-bg: #1d2021;
    --table-row-alt: #141617;
    --quote-bg: #1d2021;
    --quote-border: #0480aa;
    --heading-border: #282828;
    --nav-btn-bg: #1d2021;
    --nav-btn-border: #3c3836;
    --nav-btn-fg: #c5bda7;
  }

  html.navy {
    --bg: #161923;
    --fg: #bcbdd0;
    --fg-muted: #848a9f;
    --sidebar-bg: #1f2330;
    --sidebar-fg: #bcbdd0;
    --sidebar-border: #2b3144;
    --sidebar-active: #39a9dd;
    --sidebar-active-bg: #39a9dd22;
    --menubar-bg: #161923;
    --menubar-border: #2b3144;
    --links: #39a9dd;
    --links-hover: #5ec0ee;
    --inline-code: #e5e6f0;
    --inline-code-bg: #252b3c;
    --inline-code-border: #2b3144;
    --code-bg: #1f2330;
    --code-header: #161923;
    --code-border: #2b3144;
    --code-fg: #e5e6f0;
    --table-border: #2b3144;
    --table-header-bg: #1f2330;
    --table-row-alt: #161923;
    --quote-bg: #1f2330;
    --quote-border: #39a9dd;
    --heading-border: #2b3144;
    --nav-btn-bg: #1f2330;
    --nav-btn-border: #2b3144;
    --nav-btn-fg: #bcbdd0;
  }

  html.ayu {
    --bg: #0f141c;
    --fg: #c5c5c5;
    --fg-muted: #737d8c;
    --sidebar-bg: #141925;
    --sidebar-fg: #c5c5c5;
    --sidebar-border: #1e2536;
    --sidebar-active: #ffb454;
    --sidebar-active-bg: #ffb45422;
    --menubar-bg: #0f141c;
    --menubar-border: #1e2536;
    --links: #ffb454;
    --links-hover: #ffc980;
    --inline-code: #e6e1cf;
    --inline-code-bg: #19202f;
    --inline-code-border: #1e2536;
    --code-bg: #141925;
    --code-header: #0f141c;
    --code-border: #1e2536;
    --code-fg: #e6e1cf;
    --table-border: #1e2536;
    --table-header-bg: #141925;
    --table-row-alt: #0f141c;
    --quote-bg: #141925;
    --quote-border: #ffb454;
    --heading-border: #1e2536;
    --nav-btn-bg: #141925;
    --nav-btn-border: #1e2536;
    --nav-btn-fg: #c5c5c5;
  }

  /* ─── Syntax Highlighter Tokens ────────────────────────────────────── */
  :root {
    --tok-cmt: #6a737d;
    --tok-str: #98c379;
    --tok-kw: #e06c75;
    --tok-type: #4ec9b0;
    --tok-prep: #d19a66;
    --tok-num: #d19a66;
    --tok-fn: #61afef;
    --tok-bool: #c678dd;
    --tok-hdr: #e5c07b;
    --tok-key: #abb2bf;
    --tok-cmd: #61afef;
    --tok-opt: #e5c07b;
  }

  /* Base Reset */
  * { box-sizing: border-box; margin: 0; padding: 0; }

  html {
    scroll-behavior: smooth;
    font-size: 16px;
  }

  body {
    background-color: var(--bg);
    color: var(--fg);
    font-family: "Open Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Oxygen, Ubuntu, Cantarell, "Fira Sans", "Droid Sans", "Helvetica Neue", sans-serif;
    line-height: 1.68;
    text-rendering: optimizeLegibility;
    -webkit-font-smoothing: antialiased;
    overflow-x: hidden;
  }

  a {
    color: var(--links);
    text-decoration: none;
    transition: color 0.15s ease;
  }
  a:hover {
    color: var(--links-hover);
    text-decoration: underline;
  }

  /* ─── Layout: Sidebar & Content ────────────────────────────────────── */
  .layout-container {
    display: flex;
    min-height: 100vh;
  }

  /* Sidebar */
  .sidebar {
    position: fixed;
    top: 0;
    bottom: 0;
    left: 0;
    width: 300px;
    background-color: var(--sidebar-bg);
    border-right: 1px solid var(--sidebar-border);
    overflow-y: auto;
    z-index: 100;
    transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  }

  .sidebar.hidden {
    transform: translateX(-300px);
  }

  .sidebar-header {
    padding: 16px 20px;
    border-bottom: 1px solid var(--sidebar-border);
  }

  .sidebar-title {
    font-size: 1.05rem;
    font-weight: 700;
    color: var(--sidebar-fg);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .sidebar-version {
    font-size: 0.75rem;
    font-weight: 600;
    color: var(--links);
    background: var(--sidebar-active-bg);
    padding: 2px 6px;
    border-radius: 4px;
  }

  .sidebar-search-box {
    margin-top: 10px;
  }

  .sidebar-search-box input {
    width: 100%;
    padding: 6px 10px;
    font-size: 0.82rem;
    border: 1px solid var(--sidebar-border);
    border-radius: 4px;
    background: var(--bg);
    color: var(--fg);
    outline: none;
  }

  .sidebar-search-box input:focus {
    border-color: var(--links);
  }

  .sidebar-scrollbox {
    padding: 12px 0 30px;
  }

  .chapter-list {
    list-style: none;
  }

  .chapter-item {
    margin: 1px 0;
  }

  .chapter-link {
    display: block;
    padding: 7px 20px;
    color: var(--sidebar-fg);
    font-size: 0.87rem;
    line-height: 1.35;
    text-decoration: none;
    border-left: 3px solid transparent;
    transition: all 0.12s ease;
  }

  .chapter-link:hover {
    color: var(--links);
    background: var(--sidebar-active-bg);
    text-decoration: none;
  }

  .chapter-link.active {
    font-weight: 700;
    color: var(--sidebar-active);
    border-left-color: var(--sidebar-active);
    background: var(--sidebar-active-bg);
  }

  /* Page Wrapper */
  .page-wrapper {
    flex: 1;
    margin-left: 300px;
    display: flex;
    flex-direction: column;
    min-width: 0;
    transition: margin-left 0.2s cubic-bezier(0.4, 0, 0.2, 1);
  }

  .page-wrapper.sidebar-hidden {
    margin-left: 0;
  }

  /* ─── Top Menu Bar ─────────────────────────────────────────────────── */
  .menu-bar {
    position: sticky;
    top: 0;
    height: 52px;
    background-color: var(--menubar-bg);
    border-bottom: 1px solid var(--menubar-border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    z-index: 90;
  }

  .menu-bar-left, .menu-bar-right {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .menu-title {
    font-size: 0.95rem;
    font-weight: 600;
    color: var(--fg);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    text-align: center;
    padding: 0 12px;
  }

  .icon-button {
    background: transparent;
    border: none;
    cursor: pointer;
    color: var(--fg);
    width: 32px;
    height: 32px;
    border-radius: 4px;
    display: flex;
    align-items: center;
    justify-content: center;
    opacity: 0.75;
    transition: opacity 0.15s ease, background-color 0.15s ease;
  }

  .icon-button:hover {
    opacity: 1;
    background-color: var(--sidebar-active-bg);
  }

  /* Theme Popup */
  .theme-popup-wrapper {
    position: relative;
  }

  .theme-popup {
    display: none;
    position: absolute;
    top: calc(100% + 4px);
    left: 0;
    background: var(--bg);
    border: 1px solid var(--sidebar-border);
    border-radius: 6px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    list-style: none;
    min-width: 110px;
    padding: 4px 0;
    z-index: 150;
  }

  .theme-popup.show {
    display: block;
  }

  .theme-option {
    width: 100%;
    padding: 6px 16px;
    border: none;
    background: transparent;
    color: var(--fg);
    text-align: left;
    font-size: 0.85rem;
    cursor: pointer;
  }

  .theme-option:hover {
    background: var(--sidebar-active-bg);
    color: var(--links);
  }

  /* ─── Content Area ─────────────────────────────────────────────────── */
  .content-container {
    max-width: 820px;
    width: 100%;
    margin: 0 auto;
    padding: 40px 24px 80px;
    flex: 1;
  }

  /* Header hero */
  .book-hero {
    margin-bottom: 40px;
    padding-bottom: 24px;
    border-bottom: 1px solid var(--heading-border);
  }

  .book-hero h1 {
    font-size: 2.2rem;
    font-weight: 700;
    letter-spacing: -0.02em;
    color: var(--fg);
    margin-bottom: 10px;
    border-bottom: none;
  }

  .book-hero p {
    font-size: 1.05rem;
    color: var(--fg-muted);
  }

  /* Chapter Sections */
  .chapter-section {
    padding-top: 20px;
    margin-bottom: 60px;
  }

  h1, h2, h3, h4 {
    color: var(--fg);
    font-weight: 600;
    scroll-margin-top: 70px;
  }

  h1 {
    font-size: 1.85rem;
    margin: 32px 0 16px;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--heading-border);
  }

  h2 {
    font-size: 1.4rem;
    margin: 28px 0 14px;
    padding-bottom: 6px;
    border-bottom: 1px solid var(--heading-border);
  }

  h3 {
    font-size: 1.15rem;
    margin: 22px 0 10px;
  }

  p {
    margin: 14px 0;
  }

  ul, ol {
    margin: 14px 0 14px 24px;
  }

  li {
    margin: 6px 0;
  }

  code {
    font-family: "Source Code Pro", Menlo, Monaco, Consolas, "Liberation Mono", monospace;
    font-size: 0.88em;
  }

  p code, li code, td code, th code {
    background-color: var(--inline-code-bg);
    color: var(--inline-code);
    border: 1px solid var(--inline-code-border);
    padding: 2px 5px;
    border-radius: 3px;
  }

  /* Tables */
  .table-container {
    overflow-x: auto;
    margin: 20px 0;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.88rem;
    border: 1px solid var(--table-border);
  }

  th {
    background-color: var(--table-header-bg);
    color: var(--fg);
    font-weight: 600;
    text-align: left;
    padding: 10px 14px;
    border: 1px solid var(--table-border);
  }

  td {
    padding: 9px 14px;
    border: 1px solid var(--table-border);
    vertical-align: top;
  }

  tr:nth-child(even) {
    background-color: var(--table-row-alt);
  }

  /* Callouts */
  .callout {
    margin: 20px 0;
    padding: 14px 18px;
    background-color: var(--quote-bg);
    border-left: 4px solid var(--quote-border);
    border-radius: 0 4px 4px 0;
  }

  .callout-title {
    font-weight: 700;
    font-size: 0.9rem;
    color: var(--fg);
    margin-bottom: 6px;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .callout-body {
    font-size: 0.92rem;
    color: var(--fg);
  }

  .callout-warn { border-left-color: #f59e0b; }
  .callout-ban  { border-left-color: #ef4444; }
  .callout-tip  { border-left-color: #10b981; }
  .callout-spec { border-left-color: #8b5cf6; }

  /* Code Blocks */
  .code-block {
    margin: 20px 0;
    border-radius: 6px;
    border: 1px solid var(--code-border);
    overflow: hidden;
    background-color: var(--code-bg);
  }

  .code-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    background-color: var(--code-header);
    padding: 6px 14px;
    border-bottom: 1px solid var(--code-border);
  }

  .code-lang {
    font-size: 0.75rem;
    font-family: "Source Code Pro", monospace;
    font-weight: 600;
    color: #8b949e;
    letter-spacing: 0.05em;
  }

  .copy-btn {
    background: transparent;
    border: 1px solid #30363d;
    border-radius: 4px;
    color: #c9d1d9;
    font-size: 0.72rem;
    font-weight: 500;
    padding: 2px 8px;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .copy-btn:hover {
    background: #30363d;
    color: #ffffff;
  }

  .copy-btn.copied {
    color: #56d364;
    border-color: #56d364;
  }

  pre {
    margin: 0;
    padding: 14px 16px;
    overflow-x: auto;
    line-height: 1.5;
    font-size: 0.88rem;
    background: transparent;
  }

  pre code {
    color: var(--code-fg);
    background: transparent;
    border: none;
    padding: 0;
  }

  /* Architecture diagram */
  .arch-diagram {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 8px;
    margin: 20px 0;
    padding: 16px;
    background: var(--quote-bg);
    border: 1px solid var(--heading-border);
    border-radius: 6px;
  }

  .arch-box {
    padding: 8px 12px;
    border-radius: 4px;
    background: var(--bg);
    border: 1px solid var(--table-border);
    font-family: "Source Code Pro", monospace;
    font-size: 0.82rem;
    font-weight: 600;
  }

  .arch-arrow {
    color: var(--links);
    font-weight: bold;
  }

  /* Bottom Chapter Navigation */
  .nav-chapters {
    display: flex;
    justify-content: space-between;
    margin-top: 60px;
    padding-top: 24px;
    border-top: 1px solid var(--heading-border);
    gap: 16px;
  }

  .nav-chapter-btn {
    display: inline-flex;
    align-items: center;
    padding: 10px 18px;
    background: var(--nav-btn-bg);
    border: 1px solid var(--nav-btn-border);
    border-radius: 6px;
    color: var(--nav-btn-fg);
    font-size: 0.88rem;
    font-weight: 600;
    text-decoration: none;
    transition: all 0.15s ease;
  }

  .nav-chapter-btn:hover {
    border-color: var(--links);
    color: var(--links);
    text-decoration: none;
  }

  .nav-chapter-btn.disabled {
    opacity: 0.4;
    pointer-events: none;
  }

  /* ─── Responsive Adjustments ───────────────────────────────────────── */
  @media (max-width: 900px) {
    .sidebar {
      transform: translateX(-300px);
    }
    .sidebar.mobile-open {
      transform: translateX(0);
    }
    .page-wrapper {
      margin-left: 0;
    }
    .content-container {
      padding: 24px 16px 60px;
    }
  }
</style>
</head>
<body>

<div class="layout-container">
  <!-- SIDEBAR -->
  <aside id="sidebar" class="sidebar" aria-label="Table of contents">
    <div class="sidebar-header">
      <div class="sidebar-title">
        <span>Rook Language</span>
        <span class="sidebar-version">v0.7.0</span>
      </div>
      <div class="sidebar-search-box">
        <input type="text" id="search-input" placeholder="Search chapters..." aria-label="Search chapters" oninput="filterChapters(this.value)">
      </div>
    </div>
    <div class="sidebar-scrollbox">
      <ol class="chapter-list" id="chapter-list">
        <!--CHAPTER_LINKS-->
      </ol>
    </div>
  </aside>

  <!-- MAIN PAGE WRAPPER -->
  <div class="page-wrapper" id="page-wrapper">
    <!-- TOP TOOLBAR (MDBOOK STYLE) -->
    <header class="menu-bar" id="menu-bar">
      <div class="menu-bar-left">
        <button id="sidebar-toggle" class="icon-button" title="Toggle Table of Contents (t)" aria-label="Toggle Table of Contents" onclick="toggleSidebar()">
          <svg viewBox="0 0 24 24" width="20" height="20"><path fill="currentColor" d="M3 18h18v-2H3v2zm0-5h18v-2H3v2zm0-7v2h18V6H3z"/></svg>
        </button>
        <div class="theme-popup-wrapper">
          <button id="theme-toggle" class="icon-button" title="Change theme" aria-label="Change theme" onclick="toggleThemeMenu()">
            <svg viewBox="0 0 24 24" width="20" height="20"><path fill="currentColor" d="M12 3c-4.97 0-9 4.03-9 9 0 2.12.74 4.07 1.97 5.61L4.35 18.9c-.39.39-.39 1.02 0 1.41.39.39 1.02.39 1.41 0l1.3-1.3C8.42 19.64 10.13 20 12 20c4.97 0 9-4.03 9-9s-4.03-9-9-9zm0 15c-3.31 0-6-2.69-6-6s2.69-6 6-6 6 2.69 6 6-2.69 6-6 6z"/></svg>
          </button>
          <ul id="theme-list" class="theme-popup" role="menu" aria-label="Themes">
            <li role="none"><button role="menuitem" class="theme-option" onclick="setTheme('warm')">Warm</button></li>
            <li role="none"><button role="menuitem" class="theme-option" onclick="setTheme('coal')">Coal</button></li>
            <li role="none"><button role="menuitem" class="theme-option" onclick="setTheme('navy')">Navy</button></li>
            <li role="none"><button role="menuitem" class="theme-option" onclick="setTheme('ayu')">Ayu</button></li>
          </ul>
        </div>
      </div>
      <div class="menu-title">The Rook Programming Language</div>
      <div class="menu-bar-right">
        <a href="https://github.com/bknsehan/Rook" target="_blank" rel="noopener" class="icon-button" title="GitHub repository" aria-label="GitHub repository">
          <svg viewBox="0 0 24 24" width="20" height="20"><path fill="currentColor" d="M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12"/></svg>
        </a>
      </div>
    </header>

    <!-- CONTENT -->
    <main class="content-container">
      <div class="book-hero">
        <h1>The Rook Programming Language</h1>
        <p>Technical Reference &amp; Language Specification Manual (v0.7.0)</p>
      </div>

      <div class="content" id="book-content">
        <!--CHAPTERS_BODY-->
      </div>

      <!-- FOOTER NAVIGATION -->
      <nav class="nav-chapters" aria-label="Chapter navigation">
        <a href="#" id="prev-chapter-btn" class="nav-chapter-btn" onclick="navigateChapter(-1); return false;">← Previous Chapter</a>
        <a href="#" id="next-chapter-btn" class="nav-chapter-btn" onclick="navigateChapter(1); return false;">Next Chapter →</a>
      </nav>
    </main>
  </div>
</div>

<script>
  // ─── Theme Management ────────────────────────────────────────────────
  function setTheme(name) {
    document.documentElement.className = name;
    localStorage.setItem('rook-book-theme', name);
    document.getElementById('theme-list').classList.remove('show');
  }

  function toggleThemeMenu() {
    document.getElementById('theme-list').classList.toggle('show');
  }

  document.addEventListener('click', function(e) {
    var themeWrapper = document.querySelector('.theme-popup-wrapper');
    if (themeWrapper && !themeWrapper.contains(e.target)) {
      document.getElementById('theme-list').classList.remove('show');
    }
  });

  // Load saved theme or default to 'warm'
  var savedTheme = localStorage.getItem('rook-book-theme') || 'warm';
  if (savedTheme === 'light' || savedTheme === 'rust') savedTheme = 'warm';
  setTheme(savedTheme);

  // ─── Sidebar Management ──────────────────────────────────────────────
  var sidebar = document.getElementById('sidebar');
  var pageWrapper = document.getElementById('page-wrapper');

  function toggleSidebar() {
    if (window.innerWidth <= 900) {
      sidebar.classList.toggle('mobile-open');
    } else {
      sidebar.classList.toggle('hidden');
      pageWrapper.classList.toggle('sidebar-hidden');
      localStorage.setItem('rook-sidebar-state', sidebar.classList.contains('hidden') ? 'hidden' : 'open');
    }
  }

  if (window.innerWidth > 900) {
    var savedSidebar = localStorage.getItem('rook-sidebar-state');
    if (savedSidebar === 'hidden') {
      sidebar.classList.add('hidden');
      pageWrapper.classList.add('sidebar-hidden');
    }
  }

  // ─── Chapter Navigation & Active Tracking ────────────────────────────
  var chapters = [];
  var currentChapterIdx = 0;

  function initChapters() {
    var sections = document.querySelectorAll('.chapter-section');
    chapters = [];
    for (var i = 0; i < sections.length; i++) {
      chapters.push(sections[i].id);
    }
    updateNavButtons();
  }

  function updateNavButtons() {
    var prevBtn = document.getElementById('prev-chapter-btn');
    var nextBtn = document.getElementById('next-chapter-btn');
    if (!prevBtn || !nextBtn || chapters.length === 0) return;

    if (currentChapterIdx <= 0) {
      prevBtn.classList.add('disabled');
      prevBtn.style.visibility = 'hidden';
    } else {
      prevBtn.classList.remove('disabled');
      prevBtn.style.visibility = 'visible';
      var prevTitle = document.querySelector('a[data-id="' + chapters[currentChapterIdx - 1] + '"]').textContent;
      prevBtn.textContent = '← ' + prevTitle;
    }

    if (currentChapterIdx >= chapters.length - 1) {
      nextBtn.classList.add('disabled');
      nextBtn.style.visibility = 'hidden';
    } else {
      nextBtn.classList.remove('disabled');
      nextBtn.style.visibility = 'visible';
      var nextTitle = document.querySelector('a[data-id="' + chapters[currentChapterIdx + 1] + '"]').textContent;
      nextBtn.textContent = nextTitle + ' →';
    }
  }

  function navigateChapter(direction) {
    var newIdx = currentChapterIdx + direction;
    if (newIdx >= 0 && newIdx < chapters.length) {
      var targetId = chapters[newIdx];
      var targetEl = document.getElementById(targetId);
      if (targetEl) {
        targetEl.scrollIntoView({ behavior: 'smooth' });
        currentChapterIdx = newIdx;
        updateActiveSidebar(targetId);
        updateNavButtons();
      }
    }
  }

  function updateActiveSidebar(id) {
    var links = document.querySelectorAll('.chapter-link');
    for (var i = 0; i < links.length; i++) {
      if (links[i].getAttribute('data-id') === id) {
        links[i].classList.add('active');
        links[i].scrollIntoView({ block: 'nearest' });
      } else {
        links[i].classList.remove('active');
      }
    }
  }

  // Track active chapter on scroll
  window.addEventListener('scroll', function() {
    var sections = document.querySelectorAll('.chapter-section');
    var scrollPos = window.scrollY + 120;
    var current = chapters[0];
    for (var i = 0; i < sections.length; i++) {
      if (sections[i].offsetTop <= scrollPos) {
        current = sections[i].id;
        currentChapterIdx = i;
      }
    }
    updateActiveSidebar(current);
    updateNavButtons();
  }, { passive: true });

  // ─── Filter Chapters Search ──────────────────────────────────────────
  function filterChapters(query) {
    var q = query.toLowerCase().trim();
    var links = document.querySelectorAll('.chapter-link');
    for (var i = 0; i < links.length; i++) {
      var text = links[i].textContent.toLowerCase();
      var item = links[i].parentElement;
      if (!q || text.indexOf(q) > -1) {
        item.style.display = 'block';
      } else {
        item.style.display = 'none';
      }
    }
  }

  // ─── Keyboard Shortcuts ──────────────────────────────────────────────
  document.addEventListener('keydown', function(e) {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
    if (e.key === 'ArrowLeft') {
      navigateChapter(-1);
    } else if (e.key === 'ArrowRight') {
      navigateChapter(1);
    } else if (e.key === 't' || e.key === 'T') {
      toggleSidebar();
    } else if (e.key === 's' || e.key === 'S' || e.key === '/') {
      e.preventDefault();
      var input = document.getElementById('search-input');
      if (input) input.focus();
    }
  });

  // ─── Code Copy & Highlighting ────────────────────────────────────────
  function escapeHtml(str) {
    return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function highlightCAndRook(src) {
    var masterRegex = /(#[^\n]*|\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(^\s*#(?:include|comprise|define|ifdef|ifndef|endif)\b[^\n]*)|(\b(?:fn|int|float|double|char|void|bool|size_t|uint8_t|int8_t|uint16_t|int16_t|uint32_t|int32_t|uint64_t|int64_t|struct|enum|union|sum|impl|match|defer|return|if|else|while|for|break|continue|as|true|false|NULL|null)\b)|(\b[A-Z][a-zA-Z0-9_]*\b)|(\b\d+(?:\.\d+)?(?:[fF]|[uU]|[lL]{1,2})?\b)|(\b[a-zA-Z_][a-zA-Z0-9_]*(?=\s*\())/gm;
    var lastIndex = 0;
    var out = "";
    var match;
    while ((match = masterRegex.exec(src)) !== null) {
      if (match.index > lastIndex) {
        out += escapeHtml(src.substring(lastIndex, match.index));
      }
      var cmt = match[1], str = match[2], prep = match[3], kw = match[4], type = match[5], num = match[6], fn = match[7];
      if (cmt) out += '<span style="color:var(--tok-cmt)">' + escapeHtml(cmt) + '</span>';
      else if (str) out += '<span style="color:var(--tok-str)">' + escapeHtml(str) + '</span>';
      else if (prep) out += '<span style="color:var(--tok-prep)">' + escapeHtml(prep) + '</span>';
      else if (kw) out += '<span style="color:var(--tok-kw)">' + escapeHtml(kw) + '</span>';
      else if (type) out += '<span style="color:var(--tok-type)">' + escapeHtml(type) + '</span>';
      else if (num) out += '<span style="color:var(--tok-num)">' + escapeHtml(num) + '</span>';
      else if (fn) out += '<span style="color:var(--tok-fn)">' + escapeHtml(fn) + '</span>';
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
      if (cmt) out += '<span style="color:var(--tok-cmt)">' + escapeHtml(cmt) + '</span>';
      else if (hdr) out += '<span style="color:var(--tok-hdr)">' + escapeHtml(hdr) + '</span>';
      else if (str) out += '<span style="color:var(--tok-str)">' + escapeHtml(str) + '</span>';
      else if (key) out += '<span style="color:var(--tok-key)">' + escapeHtml(key) + '</span>';
      else if (boolean) out += '<span style="color:var(--tok-bool)">' + escapeHtml(boolean) + '</span>';
      else if (num) out += '<span style="color:var(--tok-num)">' + escapeHtml(num) + '</span>';
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
      if (cmt) out += '<span style="color:var(--tok-cmt)">' + escapeHtml(cmt) + '</span>';
      else if (str) out += '<span style="color:var(--tok-str)">' + escapeHtml(str) + '</span>';
      else if (cmd) out += '<span style="color:var(--tok-cmd)">' + escapeHtml(cmd) + '</span>';
      else if (opt) out += '<span style="color:var(--tok-opt)">' + escapeHtml(opt) + '</span>';
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
      } else if (className.indexOf('language-bash') > -1) {
        codeEl.innerHTML = highlightBash(raw);
      }
    }
  }

  function copyCode(btn) {
    var block = btn.closest('.code-block');
    if (!block) return;
    var codeEl = block.querySelector('pre code');
    if (!codeEl) return;
    var text = codeEl.textContent;
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(function() {
        btn.textContent = 'Copied!';
        btn.classList.add('copied');
        setTimeout(function() { btn.textContent = 'Copy'; btn.classList.remove('copied'); }, 1800);
      });
    } else {
      var ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      try {
        document.execCommand('copy');
        btn.textContent = 'Copied!';
        btn.classList.add('copied');
        setTimeout(function() { btn.textContent = 'Copy'; btn.classList.remove('copied'); }, 1800);
      } catch (e) {}
      document.body.removeChild(ta);
    }
  }

  document.addEventListener('DOMContentLoaded', function() {
    initChapters();
    highlightAllCodeBlocks();
  });
</script>
</body>
</html>
"""

def main():
    print(f"Generating mdBook-styled documentation for Rook (v0.7.0)...")
    chapters = get_guide_chapters(make_code_box, make_callout)

    chapter_links = []
    chapter_sections = []

    for cid, title, content in chapters:
        chapter_links.append(f'<li class="chapter-item"><a href="#{cid}" class="chapter-link" data-id="{cid}">{title}</a></li>')
        sec = f"""
<section class="chapter-section" id="{cid}">
  <h1>{title}</h1>
  {content}
</section>
"""
        chapter_sections.append(sec)

    nav_html = "\n        ".join(chapter_links)
    body_html = "\n".join(chapter_sections)

    html = HTML_TEMPLATE.replace("<!--CHAPTER_LINKS-->", nav_html)
    html = html.replace("<!--CHAPTERS_BODY-->", body_html)

    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(html)

    print(f"Successfully generated {OUTPUT_FILE} ({len(html)} bytes).")

if __name__ == "__main__":
    main()
