/* Loading screen — no Three.js, no GSAP */

window.revealApp = function () {
  document.getElementById('loading-screen').classList.add('hidden');
  document.getElementById('app').classList.remove('hidden');
  if (window.MAP) MAP.getMap().invalidateSize();
};

(function () {
  const msgs = [
    'Loading spatial index…',
    'Building KD-Tree…',
    'Fetching Pune facilities…',
    'Initialising Voronoi diagram…',
    'Ready.',
  ];
  const tw = document.getElementById('typewriter');
  let mIdx = 0, cIdx = 0, dir = 1;
  let timer;

  function type() {
    const msg = msgs[mIdx];
    if (dir === 1) {
      tw.textContent = msg.slice(0, ++cIdx);
      if (cIdx === msg.length) {
        dir = -1;
        timer = setTimeout(type, mIdx === msgs.length - 1 ? 800 : 1200);
        return;
      }
    } else {
      tw.textContent = msg.slice(0, --cIdx);
      if (cIdx === 0) { dir = 1; mIdx = (mIdx + 1) % msgs.length; }
    }
    timer = setTimeout(type, dir === 1 ? 55 : 30);
  }
  type();
})();
