/* Three.js interlocking rings loading screen */
(function () {
  // Fallback revealApp defined first so boot() never crashes even if Three.js failed
  window.revealApp = function () {
    const loadEl = document.getElementById('loading-screen');
    const appEl  = document.getElementById('app');
    appEl.classList.remove('hidden');
    loadEl.classList.add('hidden');
  };

  if (typeof THREE === 'undefined') return;  // no Three.js — fallback already set

  const canvas   = document.getElementById('loading-canvas');
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(window.devicePixelRatio);

  const scene  = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.1, 100);
  camera.position.z = 4;

  // Two torus rings
  const mat1 = new THREE.MeshBasicMaterial({ color: 0x6C63FF, wireframe: true });
  const mat2 = new THREE.MeshBasicMaterial({ color: 0x00E5CC, wireframe: true });
  const geo  = new THREE.TorusGeometry(1, 0.06, 12, 80);
  const ring1 = new THREE.Mesh(geo, mat1);
  const ring2 = new THREE.Mesh(geo, mat2);
  ring2.rotation.x = Math.PI / 2;
  scene.add(ring1, ring2);

  let rafId;
  function animate() {
    rafId = requestAnimationFrame(animate);
    ring1.rotation.y += 0.012;
    ring1.rotation.z += 0.006;
    ring2.rotation.z += 0.012;
    ring2.rotation.x += 0.006;
    renderer.render(scene, camera);
  }
  animate();

  // Typewriter messages
  const tw   = document.getElementById('typewriter');
  const msgs = [
    'Loading spatial index…',
    'Building KD-Tree…',
    'Fetching Pune facilities…',
    'Initialising Voronoi diagram…',
    'Ready.',
  ];
  let mIdx = 0, cIdx = 0, dir = 1;
  let twTimer;

  function type() {
    const msg = msgs[mIdx];
    if (dir === 1) {
      tw.textContent = msg.slice(0, ++cIdx);
      if (cIdx === msg.length) {
        dir = -1;
        twTimer = setTimeout(type, mIdx === msgs.length - 1 ? 800 : 1200);
        return;
      }
    } else {
      tw.textContent = msg.slice(0, --cIdx);
      if (cIdx === 0) {
        dir = 1;
        mIdx = (mIdx + 1) % msgs.length;
      }
    }
    twTimer = setTimeout(type, dir === 1 ? 55 : 30);
  }
  type();

  // Override with the full animated version
  window.revealApp = function () {
    clearTimeout(twTimer);
    tw.textContent = 'Ready.';

    const loadEl = document.getElementById('loading-screen');
    const appEl  = document.getElementById('app');
    appEl.classList.remove('hidden');
    appEl.style.clipPath = 'inset(50% 0 50% 0)';

    gsap.to(appEl, {
      clipPath: 'inset(0% 0 0% 0)',
      duration: 0.9,
      ease: 'power3.inOut',
      onComplete: () => {
        appEl.style.clipPath = '';
        gsap.to(loadEl, {
          opacity: 0, duration: 0.4,
          onComplete: () => {
            loadEl.classList.add('hidden');
            cancelAnimationFrame(rafId);
            renderer.dispose();
            // Force Leaflet to recalculate tile layout after reveal
            if (window.MAP) MAP.getMap().invalidateSize();
          }
        });
      }
    });
  };

  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });
})();
