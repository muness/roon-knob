// Firmware-owned fork of https://github.com/open-horizon-labs/hiphi/blob/main/site.js
// Keep this aligned with the HiPhi site when navigation behavior changes.
(() => {
  const subject = document.getElementById('preview-subject');
  if (subject?.dataset.subjectB64) {
    const bytes = Uint8Array.from(atob(subject.dataset.subjectB64), c => c.charCodeAt(0));
    subject.textContent = new TextDecoder().decode(bytes);
  }

  const targetNames = {
    dial: 'HiPhi Dial', frame: 'HiPhi Frame', rlcd: 'HiPhi Slate',
    joy: 'HiPhi Joy', halo: 'HiPhi HALO', tough: 'HiPhi Tough', m5dial: 'HiPhi Dial Lab',
    sticks3: 'HiPhi Twist', stopwatch: 'HiPhi Remote', stackchan: 'Kizz',
    knobaux: 'Dial auxiliary parking image'
  };
  const requestedTarget = new URLSearchParams(location.search).get('target');
  const hasTarget = target => Object.hasOwn(targetNames, target);
  const target = hasTarget(requestedTarget) ? requestedTarget : document.body.dataset.targetFilter;
  if (hasTarget(target)) {
    document.querySelectorAll('[data-channel-link], [data-preserve-target]').forEach(link => {
      const url = new URL(link.getAttribute('href'), location.href);
      url.searchParams.set('target', target);
      link.href = url.href;
    });
    const list = document.querySelector('.flash-list');
    const heading = document.querySelector('h1');
    if (list) {
      const allowedCards = target === 'dial'
        ? new Set(['dial-card', 'knobaux-card'])
        : new Set([`${target}-card`]);
      list.querySelectorAll('.flash-card').forEach(card => {
        if (!allowedCards.has(card.id)) card.remove();
      });
      if (heading) heading.textContent = `Flash ${targetNames[target]}`;
      if (!list.querySelector('.flash-card')) {
        const notice = document.createElement('p');
        notice.className = 'channel-notice';
        notice.textContent = `${targetNames[target]} is not listed in this release. Choose another channel or check the release notes.`;
        list.append(notice);
      }
    } else if (heading) {
      heading.textContent = `Choose a release for ${targetNames[target]}.`;
    }
  }
  const channel = document.body.dataset.channel;
  document.querySelector(`[data-channel-link="${channel}"]`)?.setAttribute('aria-current', 'page');

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
    } else if (!window.isSecureContext) {
      notice.dataset.browserState = 'unsupported';
      status.textContent = 'USB flashing requires a secure page. Open https://firmware.hiphi.audio/ to install.';
    } else if ('serial' in navigator) {
      notice.dataset.browserState = 'supported';
      status.textContent = 'This browser supports the USB connection required by the flasher.';
    } else {
      notice.dataset.browserState = 'unsupported';
      status.textContent = 'This browser cannot connect over USB. Open this page in a current desktop version of Chrome, Edge, or Firefox to install.';
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
