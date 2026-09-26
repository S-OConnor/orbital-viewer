/* Orbital LOS Viewer documentation — shared site script.
   Owns the navigation tree (single source of truth), and renders the
   sidebar, breadcrumbs, prev/next links, heading anchors, search, and
   syntax highlighting on every page. No external dependencies.

   Page contract:
     <body data-page="user-guide/installation" data-root="..">
     <nav class="sidebar" id="sidebar"></nav>
     <nav class="breadcrumbs" id="breadcrumbs"></nav>
     <div class="page-nav" id="page-nav"></div>   (optional)
   Code blocks: <pre><code class="language-sh|cpp|js|toml|json|csv"> */

(function () {
  "use strict";

  var NAV = [
    { title: "Start", items: [
      { path: "index", label: "Home" }
    ]},
    { title: "Overview", items: [
      { path: "overview/overview",     label: "Project Overview" },
      { path: "overview/architecture", label: "Architecture at a Glance" },
      { path: "overview/repository",   label: "Repository & Technology" },
      { path: "overview/faq",          label: "FAQ" }
    ]},
    { title: "User Guide", items: [
      { path: "user-guide/installation",    label: "Installation" },
      { path: "user-guide/configuration",   label: "Configuration" },
      { path: "user-guide/getting-started", label: "Getting Started" },
      { path: "user-guide/usage",           label: "Usage" },
      { path: "user-guide/tutorials",       label: "Tutorials" },
      { path: "user-guide/troubleshooting", label: "Troubleshooting" },
      { path: "user-guide/best-practices",  label: "Best Practices" }
    ]},
    { title: "Developer Guide", items: [
      { path: "developer-guide/repo-structure",     label: "Repository Structure" },
      { path: "developer-guide/architecture",       label: "Architecture in Depth" },
      { path: "developer-guide/build-system",       label: "Build System" },
      { path: "developer-guide/source-walkthrough", label: "Source Walkthrough" },
      { path: "developer-guide/api",                label: "API Documentation" },
      { path: "developer-guide/extending",          label: "Extending the Software" },
      { path: "developer-guide/new-interface",      label: "Adding a New Interface" },
      { path: "developer-guide/testing",            label: "Testing" },
      { path: "developer-guide/debugging",          label: "Debugging" },
      { path: "developer-guide/coding-standards",   label: "Coding Standards" },
      { path: "developer-guide/release-process",    label: "Release Process" }
    ]},
    { title: "Reference", items: [
      { path: "reference/configuration", label: "Configuration Reference" },
      { path: "reference/cli",           label: "Command-Line Reference" },
      { path: "reference/protocol-udp",  label: "OLV1 UDP Protocol" },
      { path: "reference/protocol-dis",  label: "DIS Input Protocol" },
      { path: "reference/protocol-olv2", label: "OLV2 UDP Protocol" },
      { path: "reference/protocol-ws",   label: "WebSocket Protocol" },
      { path: "reference/file-formats",  label: "File Formats" },
      { path: "reference/glossary",      label: "Glossary & Acronyms" }
    ]}
  ];

  var SECTION_OF = {};
  var FLAT = [];
  NAV.forEach(function (sec) {
    sec.items.forEach(function (it) {
      SECTION_OF[it.path] = sec.title;
      FLAT.push(it);
    });
  });

  var body = document.body;
  var PAGE = body.getAttribute("data-page") || "index";
  var ROOT = body.getAttribute("data-root") || ".";

  function href(path) { return ROOT + "/" + path + ".html"; }

  /* ---------- Sidebar ---------- */
  var sidebar = document.getElementById("sidebar");
  if (sidebar) {
    NAV.forEach(function (sec) {
      if (sec.title !== "Start") {
        var h = document.createElement("h2");
        h.textContent = sec.title;
        sidebar.appendChild(h);
      }
      var ul = document.createElement("ul");
      sec.items.forEach(function (it) {
        var li = document.createElement("li");
        var a = document.createElement("a");
        a.href = href(it.path);
        a.textContent = it.label;
        if (it.path === PAGE) a.className = "current";
        li.appendChild(a);
        ul.appendChild(li);
      });
      sidebar.appendChild(ul);
    });
  }

  var toggle = document.getElementById("nav-toggle");
  if (toggle && sidebar) {
    toggle.addEventListener("click", function () {
      sidebar.classList.toggle("open");
    });
  }

  /* ---------- Breadcrumbs ---------- */
  var bc = document.getElementById("breadcrumbs");
  if (bc && PAGE !== "index") {
    var cur = FLAT.filter(function (it) { return it.path === PAGE; })[0];
    var parts = [];
    parts.push('<a href="' + href("index") + '">Home</a>');
    if (cur && SECTION_OF[PAGE] && SECTION_OF[PAGE] !== "Start") {
      parts.push("<span>" + SECTION_OF[PAGE] + "</span>");
    }
    parts.push("<span>" + (cur ? cur.label : PAGE) + "</span>");
    bc.innerHTML = parts.join('<span class="sep">›</span>');
  }

  /* ---------- Prev / next ---------- */
  var pn = document.getElementById("page-nav");
  if (pn) {
    var idx = FLAT.map(function (it) { return it.path; }).indexOf(PAGE);
    var html = "";
    if (idx > 0) {
      var p = FLAT[idx - 1];
      html += '<a href="' + href(p.path) + '">&larr; ' + p.label + "</a>";
    } else {
      html += "<span></span>";
    }
    if (idx >= 0 && idx < FLAT.length - 1) {
      var n = FLAT[idx + 1];
      html += '<a href="' + href(n.path) + '">' + n.label + " &rarr;</a>";
    }
    pn.innerHTML = html;
  }

  /* ---------- Heading anchors ---------- */
  Array.prototype.forEach.call(
    document.querySelectorAll("main h2[id], main h3[id]"),
    function (h) {
      var a = document.createElement("a");
      a.className = "anchor";
      a.href = "#" + h.id;
      a.textContent = "¶";
      a.setAttribute("aria-label", "Link to this section");
      h.appendChild(a);
    }
  );

  /* ---------- Syntax highlighting ---------- */
  function esc(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  var LANGS = {
    sh: {
      comment: /(^|\s)#[^\n]*/gm,
      string: /"(?:[^"\\]|\\.)*"|'[^']*'/g,
      keyword: /\b(if|then|else|elif|fi|for|in|do|done|while|case|esac|function|exit|return|export|local|set|trap|sudo|cd)\b/g,
      number: null
    },
    cpp: {
      comment: /\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
      string: /"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)'/g,
      keyword: /\b(auto|bool|break|case|catch|char|class|const|constexpr|continue|default|delete|double|else|enum|explicit|extern|false|float|for|friend|if|inline|int|long|mutable|namespace|new|noexcept|nullptr|operator|override|private|protected|public|return|short|signed|sizeof|static|struct|switch|template|this|throw|true|try|typedef|typename|uint8_t|uint16_t|uint32_t|uint64_t|int32_t|size_t|union|unsigned|using|virtual|void|volatile|while)\b/g,
      number: /\b(0x[0-9a-fA-F]+|\d+(\.\d+)?[uUlLfF]*)\b/g
    },
    js: {
      comment: /\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
      string: /"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`/g,
      keyword: /\b(async|await|break|case|catch|class|const|continue|default|delete|do|else|export|extends|false|finally|for|function|if|import|in|instanceof|let|new|null|of|return|static|switch|this|throw|true|try|typeof|undefined|var|while|yield)\b/g,
      number: /\b(0x[0-9a-fA-F]+|\d+(\.\d+)?)\b/g
    },
    toml: {
      comment: /#[^\n]*/g,
      string: /"(?:[^"\\]|\\.)*"/g,
      keyword: /^\s*\[[^\]]+\]/gm,
      number: /\b(\d+(\.\d+)?|true|false)\b/g
    },
    json: {
      comment: null,
      string: /"(?:[^"\\]|\\.)*"/g,
      keyword: null,
      number: /\b(-?\d+(\.\d+)?([eE][+-]?\d+)?|true|false|null)\b/g
    },
    csv: { comment: /#[^\n]*/g, string: null, keyword: null, number: null }
  };

  function highlight(src, lang) {
    var spec = LANGS[lang];
    if (!spec) return esc(src);
    // Tokenize by priority: comments, strings, keywords, numbers.
    var tokens = []; // {start, end, cls}
    function claim(re, cls) {
      if (!re) return;
      re.lastIndex = 0;
      var m;
      while ((m = re.exec(src)) !== null) {
        if (m.index === re.lastIndex) re.lastIndex++;
        var s = m.index, e = m.index + m[0].length;
        var clash = tokens.some(function (t) { return s < t.end && e > t.start; });
        if (!clash) tokens.push({ start: s, end: e, cls: cls });
      }
    }
    claim(spec.comment, "tok-c");
    claim(spec.string, "tok-s");
    claim(spec.keyword, "tok-k");
    claim(spec.number, "tok-n");
    tokens.sort(function (a, b) { return a.start - b.start; });
    var out = "", pos = 0;
    tokens.forEach(function (t) {
      out += esc(src.slice(pos, t.start));
      out += '<span class="' + t.cls + '">' + esc(src.slice(t.start, t.end)) + "</span>";
      pos = t.end;
    });
    out += esc(src.slice(pos));
    return out;
  }

  Array.prototype.forEach.call(
    document.querySelectorAll("pre code[class*='language-']"),
    function (code) {
      var m = code.className.match(/language-([a-z]+)/);
      if (!m) return;
      code.innerHTML = highlight(code.textContent, m[1]);
    }
  );

  /* ---------- Search ---------- */
  var input = document.getElementById("search");
  var results = document.getElementById("search-results");
  if (input && results) {
    var INDEX = (window.SEARCH_INDEX || []).concat(
      FLAT.map(function (it) {
        return { page: it.path, title: it.label, section: "", anchor: "", text: it.label };
      })
    );
    function runSearch(q) {
      q = q.trim().toLowerCase();
      results.innerHTML = "";
      if (q.length < 2) { results.classList.remove("open"); return; }
      var terms = q.split(/\s+/);
      var hits = [];
      INDEX.forEach(function (e) {
        var hay = (e.title + " " + e.section + " " + (e.text || "")).toLowerCase();
        var ok = terms.every(function (t) { return hay.indexOf(t) !== -1; });
        if (ok) hits.push(e);
      });
      var seen = {};
      hits = hits.filter(function (e) {
        var k = e.page + "#" + (e.anchor || "");
        if (seen[k]) return false;
        seen[k] = true;
        return true;
      }).slice(0, 20);
      if (!hits.length) {
        results.innerHTML = '<div class="empty">No matches.</div>';
      } else {
        hits.forEach(function (e) {
          var a = document.createElement("a");
          a.href = href(e.page) + (e.anchor ? "#" + e.anchor : "");
          a.innerHTML =
            '<div class="r-page">' + esc(e.title) + "</div>" +
            (e.section ? '<div class="r-sec">' + esc(e.section) + "</div>" : "");
          results.appendChild(a);
        });
      }
      results.classList.add("open");
    }
    input.addEventListener("input", function () { runSearch(input.value); });
    input.addEventListener("focus", function () { if (input.value) runSearch(input.value); });
    document.addEventListener("click", function (ev) {
      if (!results.contains(ev.target) && ev.target !== input) {
        results.classList.remove("open");
      }
    });
    document.addEventListener("keydown", function (ev) {
      if (ev.key === "Escape") results.classList.remove("open");
      if (ev.key === "/" && document.activeElement !== input &&
          !/INPUT|TEXTAREA/.test(document.activeElement.tagName)) {
        ev.preventDefault();
        input.focus();
      }
    });
  }
})();
