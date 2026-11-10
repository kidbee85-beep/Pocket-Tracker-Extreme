'use strict';

const menuButton = document.querySelector('.menu-toggle');
const nav = document.querySelector('#site-nav');
if (menuButton && nav) {
  const closeMenu = () => { nav.classList.remove('open'); menuButton.setAttribute('aria-expanded', 'false'); };
  menuButton.addEventListener('click', () => {
    const isOpen = nav.classList.toggle('open');
    menuButton.setAttribute('aria-expanded', String(isOpen));
  });
  nav.addEventListener('click', event => { if (event.target.closest('a')) closeMenu(); });
  document.addEventListener('keydown', event => { if (event.key === 'Escape') closeMenu(); });
}

const processContent = {
  discover: { kicker: 'DISCOVER / INTERNET ARCHIVE', title: 'Find a direction before you know a title.', copy: 'Browse by genre, try Random for inspiration or search for something you already found. A result can contain several tracks: open the result, then choose the recording you want to hear.', capture: 'archive-search' },
  preview: { kicker: 'PREVIEW / FOLLOW YOUR EARS', title: 'Listen until a phrase catches you.', copy: 'Play the selected source and seek through the track. Listen for a chord, texture, break or small detail you can build around. A recording is more useful when you know exactly which part you want.', capture: 'archive-preview' },
  mark: { kicker: 'IN / OUT / MAKE A NEW WAV', title: 'Keep the part that matters.', copy: 'Set an IN and an OUT at the useful passage, refine the markers and render the region. A short clip makes a practical first step. The result becomes a local WAV for the normal editor.', capture: 'archive-region' },
  flip: { kicker: 'SLICE / PITCH / SEQUENCE', title: 'The source is only half the idea.', copy: 'Load the new WAV into an instrument. Keep a whole musical phrase or slice selected accents, then build your own arrangement around it. Bring in drums and give the source a different rhythmic role. Keep the original source details with your clip.', capture: 'sample-editor' }
};
let captures = {};
let activeProcess = 'discover';
const processButtons = Array.from(document.querySelectorAll('[data-process]'));
function setProcess(key, focus = false) {
  const content = processContent[key];
  if (!content || !processButtons.length) return;
  activeProcess = key;
  for (const button of processButtons) {
    const active = button.dataset.process === key;
    button.classList.toggle('active', active);
    button.setAttribute('aria-selected', String(active));
    button.tabIndex = active ? 0 : -1;
    if (active && focus) button.focus();
  }
  const panel = document.querySelector('#process-panel');
  if (!panel) return;
  panel.setAttribute('aria-labelledby', 'step-' + key);
  document.querySelector('[data-process-kicker]').textContent = content.kicker;
  document.querySelector('[data-process-title]').textContent = content.title;
  document.querySelector('[data-process-copy]').textContent = content.copy;
  const capture = captures[content.capture];
  const holder = panel.querySelector('.process-capture');
  if (capture && capture.ready) {
    const image = holder.querySelector('img');
    image.src = capture.file;
    image.alt = capture.alt;
    holder.hidden = false; panel.classList.add('has-capture');
  } else { holder.hidden = true; panel.classList.remove('has-capture'); }
}
for (const button of processButtons) {
  button.addEventListener('click', () => setProcess(button.dataset.process));
  button.addEventListener('keydown', event => {
    const keys = ['ArrowLeft', 'ArrowUp', 'ArrowRight', 'ArrowDown', 'Home', 'End'];
    if (!keys.includes(event.key)) return;
    event.preventDefault();
    const current = processButtons.indexOf(button);
    let index = current;
    if (event.key === 'Home') index = 0;
    else if (event.key === 'End') index = processButtons.length - 1;
    else index = (current + (['ArrowLeft', 'ArrowUp'].includes(event.key) ? -1 : 1) + processButtons.length) % processButtons.length;
    setProcess(processButtons[index].dataset.process, true);
  });
}

fetch('data/captures.json').then(response => {
  if (!response.ok) throw new Error('Capture manifest unavailable');
  return response.json();
}).then(value => {
  captures = value;
  setProcess(activeProcess);
  for (const container of document.querySelectorAll('[data-capture]')) {
    const capture = captures[container.dataset.capture];
    if (!capture || !capture.ready) continue;
    const image = document.createElement('img');
    image.src = capture.file; image.alt = capture.alt; image.width = 640; image.height = 480; image.loading = 'lazy';
    const button = document.createElement('button');
    button.className = 'image-zoom'; button.dataset.zoom = capture.file; button.setAttribute('aria-label', 'Enlarge this app screen'); button.append(image);
    const caption = document.createElement('figcaption');
    caption.textContent = capture.caption || 'Actual native app capture, rendered on a desktop.';
    container.replaceChildren(button, caption); container.hidden = false;
  }
}).catch(() => { /* Existing screenshots and the illustrated workflow remain usable. */ });

const downloadLabels = {
  android: 'Download Android APK ↗',
  'brick-first': 'Download first-install pack ↗', 'brick-update': 'Download program update ↗',
  mini: 'Download Mini offline pack ↗', plus: 'Download Mini Plus pack ↗',
  presets: 'Factory presets ↗', source: 'Complete source ↗', 'sdl-source': 'Miyoo SDL source ↗', checksums: 'Checksums ↗'
};
fetch('data/releases.json').then(response => {
  if (!response.ok) throw new Error('Release manifest unavailable');
  return response.json();
}).then(release => {
  if (release.releaseReady) {
    for (const element of document.querySelectorAll('[data-download]')) {
      const key = element.dataset.download, asset = release.assets[key];
      if (!asset || !asset.ready) continue;
      const link = document.createElement('a');
      link.className = element.className;
      link.dataset.download = key;
      link.href = release.repository + '/releases/download/' + encodeURIComponent(release.tag) + '/' + encodeURIComponent(asset.file);
      link.textContent = downloadLabels[key] || asset.file;
      element.replaceWith(link);
    }
    for (const notice of document.querySelectorAll('[data-release-notice]')) {
      const p = notice.querySelector('p');
      p.replaceChildren();
      const title = document.createElement('strong'); title.textContent = 'TrackWerk ' + release.version + ' beta is available. ';
      p.append(title, 'Choose the package for your platform and follow the field guide. Matching source and checksums accompany the release.');
    }
  }
  if (release.film && release.film.ready) {
    for (const stage of document.querySelectorAll('[data-film-stage]')) {
      const video = stage.querySelector('video');
      if (!video) continue;
      if (video.getAttribute('src') !== release.film.file) video.src = release.film.file;
      if (release.film.poster) video.poster = release.film.poster;
      video.hidden = false;
      for (const element of stage.querySelectorAll('.film-poster,.film-pending')) element.hidden = true;
      stage.classList.add('film-ready');
    }
  }
}).catch(() => {
  for (const notice of document.querySelectorAll('[data-release-notice] p')) notice.textContent = 'Release information could not be loaded. Open this site through its local preview server, or visit the GitHub repository for the published release.';
});

const imageDialog = document.querySelector('.image-dialog');
let previousFocus;
if (imageDialog) {
  document.addEventListener('click', event => {
    const trigger = event.target.closest('[data-zoom]');
    if (!trigger) return;
    const image = trigger.querySelector('img');
    imageDialog.querySelector('img').src = trigger.dataset.zoom;
    imageDialog.querySelector('img').alt = image ? image.alt : 'Enlarged app screen';
    previousFocus = trigger;
    if (typeof imageDialog.showModal === 'function') {
      imageDialog.showModal(); document.body.classList.add('dialog-open');
    } else window.open(trigger.dataset.zoom, '_blank', 'noopener');
  });
  imageDialog.querySelector('.dialog-close').addEventListener('click', () => imageDialog.close());
  imageDialog.addEventListener('click', event => { if (event.target === imageDialog) imageDialog.close(); });
  imageDialog.addEventListener('close', () => { document.body.classList.remove('dialog-open'); if (previousFocus) previousFocus.focus(); });
}

const guideLinks = Array.from(document.querySelectorAll('.guide-nav a[href^="#"]'));
if ('IntersectionObserver' in window && guideLinks.length) {
  const observer = new IntersectionObserver(entries => {
    for (const entry of entries) if (entry.isIntersecting) {
      for (const link of guideLinks) {
        const active = link.hash === '#' + entry.target.id;
        link.classList.toggle('active', active);
        if (active) link.setAttribute('aria-current', 'location'); else link.removeAttribute('aria-current');
      }
    }
  }, { rootMargin: '-5% 0px -70% 0px', threshold: 0 });
  for (const section of document.querySelectorAll('.guide-chapter[id]')) observer.observe(section);
}

for (const button of document.querySelectorAll('[data-copy]')) {
  button.addEventListener('click', async () => {
    const source = document.querySelector(button.dataset.copy);
    if (!source) return;
    try {
      await navigator.clipboard.writeText(source.textContent.trim());
      button.textContent = 'Copied';
      setTimeout(() => { button.textContent = 'Copy text'; }, 2000);
    } catch (_) {
      const range = document.createRange(); range.selectNodeContents(source);
      const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(range);
      button.textContent = 'Text selected — copy with your keyboard';
    }
  });
}
