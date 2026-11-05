// BecomeMyScreen website. The app only runs on a Mac: on any other device, say so next to the download button.
(function () {
  var ua = navigator.userAgent || '';
  // iPadOS also reports "Macintosh"; touch support tells them apart.
  var isMac = /Macintosh|Mac OS X/.test(ua) && !(navigator.maxTouchPoints > 1);
  if (!isMac) document.documentElement.classList.add('not-mac');
})();

// The download buttons go straight to the zip of the latest GitHub release. The page itself may be older than that
// release (it is rebuilt when its text changes), so the version, size and "What's new" link are read from GitHub; if
// that does not work, the page keeps what it was built with and the button still gets the latest version.
(function () {
  var links = document.querySelectorAll('a[data-download]');
  if (!links.length || !window.fetch) return;
  var repo = /^https:\/\/github\.com\/([^\/]+\/[^\/]+)\/releases\//.exec(links[0].href);
  if (!repo) return;
  function each(selector, apply) { Array.prototype.forEach.call(document.querySelectorAll(selector), apply); }
  fetch('https://api.github.com/repos/' + repo[1] + '/releases/latest', { headers: { Accept: 'application/vnd.github+json' } })
    .then(function (response) { return response.ok ? response.json() : null; })
    .then(function (release) {
      if (!release || !release.tag_name) return;
      var zip = (release.assets || []).filter(function (asset) { return asset.name === 'BecomeMyScreen.zip'; })[0];
      if (!zip) return;
      var version = release.tag_name.replace(/^v/, '');
      var size = (zip.size / 1e6).toFixed(1) + ' MB';
      each('[data-release="version"]', function (el) { el.textContent = version; });
      each('[data-release="size"]', function (el) { el.textContent = size; });
      each('a[data-download]', function (a) { a.href = zip.browser_download_url; });
      each('a[data-release="notes"]', function (a) { a.href = release.html_url; });
    })
    .catch(function () {});
})();

// Once a download has started, the next step shows under the hero's button.
(function () {
  Array.prototype.forEach.call(document.querySelectorAll('a[data-download]'), function (link) {
    link.addEventListener('click', function () {
      Array.prototype.forEach.call(document.querySelectorAll('[data-after-download]'), function (note) { note.hidden = false; });
    });
  });
})();

// Not on a Mac: "Send the link to your Mac" opens the share sheet (AirDrop, Messages, mail) where there is one, and
// copies the link elsewhere.
(function () {
  function copy(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) return navigator.clipboard.writeText(text);
    return new Promise(function (resolve, reject) {
      var area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', '');
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      var done = false;
      try { done = document.execCommand('copy'); } catch (e) {}
      document.body.removeChild(area);
      if (done) resolve(); else reject();
    });
  }
  Array.prototype.forEach.call(document.querySelectorAll('[data-send-link]'), function (button) {
    var label = button.textContent;
    button.addEventListener('click', function () {
      var url = location.href.split('#')[0];
      if (navigator.share) {
        navigator.share({ title: document.title, url: url }).catch(function () {});
        return;
      }
      copy(url).then(function () {
        button.textContent = button.getAttribute('data-copied');
        setTimeout(function () { button.textContent = label; }, 2500);
      }, function () {});
    });
  });
})();

// The section links in the header step aside when this language's labels do not fit between the name and the button.
(function () {
  var top = document.querySelector('.top-inner');
  if (!top) return;
  function fit() {
    top.classList.remove('nav-hidden');
    if (top.scrollWidth > top.clientWidth + 1) top.classList.add('nav-hidden');
  }
  fit();
  window.addEventListener('resize', fit);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
})();

// "Watch the video" plays the promo video in a dialog. Without <dialog> support the link simply opens the file.
(function () {
  var film = document.getElementById('film');
  if (!film || typeof film.showModal !== 'function') return;
  var video = film.querySelector('video');

  function open(event) {
    event.preventDefault();
    film.showModal();
    try { video.currentTime = 0; } catch (e) {}
    var playing = video.play();
    if (playing && playing.catch) playing.catch(function () {});  // blocked autoplay: the controls are still there
  }

  Array.prototype.forEach.call(document.querySelectorAll('[data-film]'), function (link) {
    link.addEventListener('click', open);
  });
  film.querySelector('[data-film-close]').addEventListener('click', function () { film.close(); });
  // A click on the dark area around the video closes it as well.
  film.addEventListener('click', function (event) { if (event.target === film) film.close(); });
  film.addEventListener('close', function () { video.pause(); });
})();

// A language picked in the footer is remembered: the English page at the site root sends returning visitors there
// (first-time visitors go by their browser's language; see the script in the root page's <head>).
(function () {
  Array.prototype.forEach.call(document.querySelectorAll('.lang-list a[data-lang]'), function (link) {
    link.addEventListener('click', function () {
      try { localStorage.setItem('bms-lang', link.getAttribute('data-lang')); } catch (e) {}
    });
  });
})();

