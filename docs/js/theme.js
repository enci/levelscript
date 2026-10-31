// Theme: 'auto' follows the OS, 'light' / 'dark' are explicit picks (kept in localStorage).
// Loaded in <head> without defer so data-theme is set before first paint (no flash).
(function () {
  const KEY = 'ls-theme';
  const MODES = ['auto', 'light', 'dark'];
  const ICON = { auto: 'fa-circle-half-stroke', light: 'fa-sun', dark: 'fa-moon' };
  const os = window.matchMedia('(prefers-color-scheme: light)');
  const read = () => { try { return localStorage.getItem(KEY) || 'auto'; } catch (e) { return 'auto'; } };
  const write = (m) => { try { m === 'auto' ? localStorage.removeItem(KEY) : localStorage.setItem(KEY, m); } catch (e) {} };

  let mode = MODES.includes(read()) ? read() : 'auto';
  const apply = () => {
    const t = mode === 'auto' ? (os.matches ? 'light' : 'dark') : mode;
    if (document.documentElement.dataset.theme === t) return;
    document.documentElement.dataset.theme = t;
    document.dispatchEvent(new Event('ls-themechange'));   // main.js redraws the maps
  };
  apply();
  os.addEventListener('change', () => { if (mode === 'auto') apply(); });

  const label = (m) => 'Theme: ' + m + ' (click to change)';
  function button() {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'theme-toggle';
    const paint = () => {
      b.innerHTML = '<i class="fas ' + ICON[mode] + '"></i> ' + mode;
      b.title = label(mode);
      b.setAttribute('aria-label', label(mode));
    };
    b.addEventListener('click', () => {
      mode = MODES[(MODES.indexOf(mode) + 1) % MODES.length];
      write(mode);
      apply();
      paint();
    });
    paint();
    return b;
  }

  document.addEventListener('DOMContentLoaded', () => {
    const links = document.querySelector('nav .nav-links');
    if (links) {
      const li = document.createElement('li');
      li.appendChild(button());
      links.appendChild(li);
    }
    const bar = document.querySelector('.lab-head .sandbox-bar');
    if (bar) bar.appendChild(button());
  });

  // another tab changed the choice
  window.addEventListener('storage', (e) => {
    if (e.key !== KEY) return;
    mode = MODES.includes(e.newValue) ? e.newValue : 'auto';
    apply();
    document.querySelectorAll('.theme-toggle').forEach((b) => {
      b.innerHTML = '<i class="fas ' + ICON[mode] + '"></i> ' + mode;
      b.title = label(mode);
      b.setAttribute('aria-label', label(mode));
    });
  });
})();
