// Firmware-owned fork of https://github.com/open-horizon-labs/hiphi/blob/main/site.js
// Keep this aligned with the HiPhi site when navigation behavior changes.
(() => {
  const header = document.querySelector('.site-header');
  const toggle = header?.querySelector('.nav-toggle');
  const nav = header?.querySelector('.site-nav');

  if (header && toggle && nav) {
    const closeMenu = () => {
      delete header.dataset.menuOpen;
      toggle.setAttribute('aria-expanded', 'false');
      toggle.querySelector('span:first-child').textContent = 'Menu';
    };

    toggle.addEventListener('click', () => {
      const isOpen = !header.hasAttribute('data-menu-open');
      header.toggleAttribute('data-menu-open', isOpen);
      toggle.setAttribute('aria-expanded', String(isOpen));
      toggle.querySelector('span:first-child').textContent = isOpen ? 'Close' : 'Menu';
    });

    nav.addEventListener('click', (event) => {
      if (event.target.closest('a')) closeMenu();
    });

    document.addEventListener('click', (event) => {
      if (!header.contains(event.target)) closeMenu();
    });

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && header.hasAttribute('data-menu-open')) {
        closeMenu();
        toggle.focus();
      }
    });
  }

  document.querySelectorAll('.kizz-media').forEach((media) => {
    const video = media.querySelector('video');
    const button = media.querySelector('.kizz-play');
    if (!video || !button) return;

    const label = button.querySelector('.play-label');
    const icon = button.querySelector('span:last-child');
    const sync = () => {
      const playing = !video.paused;
      button.setAttribute('aria-pressed', String(playing));
      label.textContent = playing ? 'Pause Kizz' : 'Play Kizz';
      icon.textContent = playing ? 'Ⅱ' : '▶';
    };

    button.addEventListener('click', () => {
      if (video.paused) video.play();
      else video.pause();
    });
    video.addEventListener('play', sync);
    video.addEventListener('pause', sync);
  });
})();
