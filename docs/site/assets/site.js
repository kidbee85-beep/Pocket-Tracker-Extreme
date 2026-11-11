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
  "discover": {
    "kicker": "LOAD / LOCAL SAMPLES",
    "title": "Find the phrase in your library.",
    "copy": "Open a sample in the instrument browser. Start with a few bars that have a shape you can build around.",
    "capture": "sample-load"
  },
  "preview": {
    "kicker": "PLAY / LISTEN FOR THE POCKET",
    "title": "Hear where the phrase turns.",
    "copy": "Preview the sample in the editor. Listen for the accents, tails and gaps that give the phrase its feel.",
    "capture": "sample-editor"
  },
  "mark": {
    "kicker": "CHOP / AUDITION",
    "title": "Place cuts where they land naturally.",
    "copy": "Use the waveform to adjust the boundaries. Audition the slices before sequencing and leave room for their tails.",
    "capture": "sample-chop"
  },
  "flip": {
    "kicker": "PHRASE / CHAIN",
    "title": "Give the cuts a new rhythm.",
    "copy": "Place the cuts in a phrase, bring in your drums and arrange contrasting sections with chains.",
    "capture": "song-pattern"
  }
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
