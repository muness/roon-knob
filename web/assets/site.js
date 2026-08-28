// Firmware-owned fork of https://github.com/open-horizon-labs/hiphi/blob/main/site.js
// Keep this aligned with the HiPhi site when navigation behavior changes.
(() => {
  document.querySelectorAll('[data-browser-requirement]').forEach((notice) => {
    const status = notice.querySelector('[data-browser-status]');
    if (!status) return;

    const userAgent = navigator.userAgent || '';
    const isIos = /iPad|iPhone|iPod/.test(userAgent) ||
      (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
    const isAndroid = /Android/.test(userAgent);

    if (isIos) {
      notice.dataset.browserState = 'unsupported';
      status.textContent = 'This iPhone or iPad can browse releases, but it cannot flash over USB. Reopen this page on a computer to install.';
    } else if (isAndroid) {
      notice.dataset.browserState = 'unsupported';
      status.textContent = 'Android USB flashing is not supported. Reopen this page on a computer to install.';
    } else if ('serial' in navigator) {
      notice.dataset.browserState = 'supported';
      status.textContent = 'This browser supports the USB connection required by the flasher.';
    }
  });

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
