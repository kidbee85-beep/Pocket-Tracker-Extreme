/* Small progressive enhancements. The guide remains readable without JavaScript. */
(() => {
  document.querySelector('[data-print-guide]')?.addEventListener('click', () => window.print());
  const menuButton = document.querySelector('.nav-toggle');
  const nav = document.querySelector('#main-nav');
  menuButton?.addEventListener('click', () => {
    const open = menuButton.getAttribute('aria-expanded') !== 'true';
    menuButton.setAttribute('aria-expanded', String(open));
    nav?.classList.toggle('open', open);
  });
  nav?.querySelectorAll('a').forEach(link => link.addEventListener('click', () => {
    nav.classList.remove('open');
    menuButton?.setAttribute('aria-expanded', 'false');
  }));
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      nav?.classList.remove('open');
      menuButton?.setAttribute('aria-expanded', 'false');
    }
  });

  const viewer = document.querySelector('#image-viewer');
  if (viewer && typeof viewer.showModal === 'function') {
    const viewerImage = viewer.querySelector('img');
    const caption = viewer.querySelector('p');
    document.querySelectorAll('.app-shot img,.feature-card img').forEach(img => {
      const open = () => {
        viewerImage.src = img.currentSrc || img.src;
        viewerImage.alt = img.alt;
        caption.textContent = img.closest('figure')?.querySelector('figcaption')?.textContent || img.alt;
        viewer.showModal();
      };
      img.tabIndex = 0;
      img.setAttribute('role', 'button');
      img.setAttribute('aria-label', 'Enlarge image: ' + img.alt);
      img.addEventListener('click', open);
      img.addEventListener('keydown', event => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault();
          open();
        }
      });
    });
    viewer.querySelector('.viewer-close')?.addEventListener('click', () => viewer.close());
    viewer.addEventListener('click', event => {
      if (event.target === viewer) {
        const rect = viewer.getBoundingClientRect();
        if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) viewer.close();
      }
    });
  }

  let copyTimer;
  document.querySelectorAll('[data-copy]').forEach(button => {
    button.addEventListener('click', async () => {
      const status = document.querySelector('.copy-status');
      try {
        if (!navigator.clipboard?.writeText) throw new Error('Clipboard is not available');
        await navigator.clipboard.writeText(button.dataset.copy);
        if (status) status.textContent = 'Sample path copied';
      } catch {
        if (status) status.textContent = 'Select and copy the path above';
      }
      if (status) {
        clearTimeout(copyTimer);
        status.classList.add('visible');
        copyTimer = setTimeout(() => status.classList.remove('visible'), 2600);
      }
    });
  });

  const tocButton = document.querySelector('.guide-toc-toggle');
  const sidebar = document.querySelector('.guide-sidebar');
  tocButton?.addEventListener('click', () => {
    const open = tocButton.getAttribute('aria-expanded') !== 'true';
    tocButton.setAttribute('aria-expanded', String(open));
    sidebar?.classList.toggle('toc-open', open);
  });
  sidebar?.querySelectorAll('nav a').forEach(link => link.addEventListener('click', () => {
    sidebar.classList.remove('toc-open');
    tocButton?.setAttribute('aria-expanded', 'false');
  }));

  const links = [...document.querySelectorAll('.guide-sidebar nav a')];
  if ('IntersectionObserver' in window && links.length) {
    const targets = links.map(link => document.querySelector(link.getAttribute('href'))).filter(Boolean);
    const visible = new Set();
    const observer = new IntersectionObserver(entries => {
      entries.forEach(entry => entry.isIntersecting ? visible.add(entry.target.id) : visible.delete(entry.target.id));
      const current = targets.find(target => visible.has(target.id));
      if (current) links.forEach(link => {
        const active = link.getAttribute('href') === '#' + current.id;
        link.classList.toggle('active', active);
        if (active) link.setAttribute('aria-current', 'location');
        else link.removeAttribute('aria-current');
      });
    }, {rootMargin:'-100px 0px -50% 0px',threshold:0});
    targets.forEach(target => observer.observe(target));
  }
})();
