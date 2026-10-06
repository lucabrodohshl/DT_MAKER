/* Verified Twin Studio documentation site: search, screenshot viewer, theme, navigation. */
(function () {
  'use strict';

  // ---------------------------------------------------------------- theme
  var root = document.documentElement;
  var themeBtn = document.querySelector('.theme');
  function effectiveDark() {
    if (root.dataset.theme) return root.dataset.theme === 'dark';
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  }
  if (themeBtn) {
    themeBtn.addEventListener('click', function () {
      var next = effectiveDark() ? 'light' : 'dark';
      root.dataset.theme = next;
      try { localStorage.setItem('vts-docs-theme', next); } catch (e) { /* storage unavailable */ }
    });
  }

  // ---------------------------------------------------------------- mobile navigation
  var menu = document.querySelector('.menu');
  if (menu) {
    menu.addEventListener('click', function () {
      var open = document.body.classList.toggle('nav-open');
      menu.setAttribute('aria-expanded', String(open));
    });
    document.querySelectorAll('.sidebar a').forEach(function (a) {
      a.addEventListener('click', function () { document.body.classList.remove('nav-open'); });
    });
  }
  var active = document.querySelector('.sidebar a.active');
  if (active && active.scrollIntoView) active.scrollIntoView({ block: 'nearest' });

  // ---------------------------------------------------------------- "On this page" highlighting
  var tocLinks = Array.prototype.slice.call(document.querySelectorAll('.toc a'));
  if (tocLinks.length && 'IntersectionObserver' in window) {
    var byId = {};
    tocLinks.forEach(function (a) { byId[decodeURIComponent(a.hash.slice(1))] = a; });
    var visible = new Set();
    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) { if (e.isIntersecting) visible.add(e.target.id); else visible.delete(e.target.id); });
      var first = Array.prototype.find.call(document.querySelectorAll('.doc h2[id], .doc h3[id]'), function (h) { return visible.has(h.id); });
      if (first) {
        tocLinks.forEach(function (a) { a.classList.remove('current'); });
        if (byId[first.id]) byId[first.id].classList.add('current');
      }
    }, { rootMargin: '-60px 0px -60% 0px' });
    document.querySelectorAll('.doc h2[id], .doc h3[id]').forEach(function (h) { obs.observe(h); });
  }

  // ---------------------------------------------------------------- search
  var input = document.getElementById('search');
  var list = document.getElementById('results');
  var index = window.VTS_SEARCH || [];
  var selected = -1;

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]; });
  }
  function highlight(text, terms) {
    var out = escapeHtml(text);
    terms.forEach(function (t) {
      if (!t) return;
      var re = t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      // Very short terms ("3") only highlight as whole words, not inside other words.
      out = out.replace(new RegExp(t.length <= 2 ? '\\b(' + re + ')\\b' : '(' + re + ')', 'gi'), '<mark>$1</mark>');
    });
    return out;
  }
  function snippet(text, terms) {
    var lower = text.toLowerCase();
    var at = -1;
    terms.forEach(function (t) { var i = lower.indexOf(t); if (i >= 0 && (at < 0 || i < at)) at = i; });
    if (at < 0) return text.slice(0, 140);
    var start = Math.max(0, at - 50);
    return (start > 0 ? '… ' : '') + text.slice(start, start + 160) + (start + 160 < text.length ? ' …' : '');
  }
  function search(q) {
    var terms = q.toLowerCase().split(/\s+/).filter(Boolean);
    if (!terms.length) return [];
    var scored = [];
    index.forEach(function (e) {
      var title = e.t.toLowerCase();
      var text = e.x.toLowerCase();
      var score = 0;
      for (var i = 0; i < terms.length; i++) {
        var t = terms[i];
        var inTitle = title.indexOf(t) >= 0;
        var inText = text.indexOf(t) >= 0;
        if (!inTitle && !inText) return;
        score += (inTitle ? 10 : 0) + (inText ? 1 + Math.min(4, text.split(t).length - 1) * 0.5 : 0);
      }
      scored.push({ e: e, s: score });
    });
    scored.sort(function (a, b) { return b.s - a.s; });
    return scored.slice(0, 12).map(function (r) { return r.e; });
  }
  function render() {
    var q = input.value.trim();
    if (!q) { list.hidden = true; return; }
    var terms = q.toLowerCase().split(/\s+/).filter(Boolean);
    var hits = search(q);
    selected = hits.length ? 0 : -1;
    list.innerHTML = hits.length
      ? hits.map(function (h, i) {
          return '<li><a href="' + h.u + '" role="option" aria-selected="' + (i === 0) + '">' +
            '<div class="r-title">' + highlight(h.t, terms) + '</div>' +
            (h.p && h.p !== h.t ? '<div class="r-page">' + escapeHtml(h.p) + '</div>' : '') +
            '<div class="r-snip">' + highlight(snippet(h.x, terms), terms) + '</div></a></li>';
        }).join('')
      : '<li class="r-empty">No results for “' + escapeHtml(q) + '”</li>';
    list.hidden = false;
  }
  function move(delta) {
    var links = list.querySelectorAll('a');
    if (!links.length) return;
    selected = (selected + delta + links.length) % links.length;
    links.forEach(function (a, i) { a.setAttribute('aria-selected', String(i === selected)); });
    links[selected].scrollIntoView({ block: 'nearest' });
  }
  if (input && list) {
    input.addEventListener('input', render);
    input.addEventListener('focus', function () { if (input.value.trim()) render(); });
    input.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
      else if (e.key === 'Enter') {
        var links = list.querySelectorAll('a');
        if (links[selected]) window.location.href = links[selected].getAttribute('href');
      } else if (e.key === 'Escape') { list.hidden = true; input.blur(); }
    });
    document.addEventListener('click', function (e) {
      if (!e.target.closest('.search')) list.hidden = true;
    });
    document.addEventListener('keydown', function (e) {
      var typing = /INPUT|TEXTAREA/.test(document.activeElement.tagName);
      if (e.key === '/' && !typing && viewer.hidden) { e.preventDefault(); input.focus(); }
    });
  }

  // ---------------------------------------------------------------- screenshot viewer
  var viewer = document.querySelector('.viewer');
  var vImg = viewer.querySelector('img');
  var vCap = viewer.querySelector('figcaption');
  var shots = Array.prototype.slice.call(document.querySelectorAll('[data-viewer]'));
  var current = -1;
  var opener = null;

  function show(i) {
    current = (i + shots.length) % shots.length;
    var a = shots[current];
    vImg.src = a.getAttribute('href');
    vImg.alt = a.dataset.caption || '';
    vCap.textContent = (a.dataset.caption || '') + (shots.length > 1 ? '  (' + (current + 1) + ' / ' + shots.length + ')' : '');
  }
  function open(i) {
    opener = document.activeElement;
    show(i);
    viewer.hidden = false;
    document.body.style.overflow = 'hidden';
    viewer.querySelector('.v-close').focus();
  }
  function close() {
    viewer.hidden = true;
    document.body.style.overflow = '';
    if (opener && opener.focus) opener.focus();
  }
  shots.forEach(function (a, i) {
    a.addEventListener('click', function (e) { e.preventDefault(); open(i); });
  });
  viewer.querySelector('.v-close').addEventListener('click', close);
  viewer.querySelector('.v-prev').addEventListener('click', function () { show(current - 1); });
  viewer.querySelector('.v-next').addEventListener('click', function () { show(current + 1); });
  viewer.addEventListener('click', function (e) { if (e.target === viewer) close(); });
  if (shots.length < 2) {
    viewer.querySelector('.v-prev').hidden = true;
    viewer.querySelector('.v-next').hidden = true;
  }
  document.addEventListener('keydown', function (e) {
    if (viewer.hidden) return;
    if (e.key === 'Escape') close();
    else if (e.key === 'ArrowRight') show(current + 1);
    else if (e.key === 'ArrowLeft') show(current - 1);
    else if (e.key === 'Tab') {
      // keep focus inside the dialog
      var f = viewer.querySelectorAll('button:not([hidden])');
      var first = f[0], last = f[f.length - 1];
      if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
    }
  });
})();
