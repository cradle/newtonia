// Lightweight parallax starfield for the Newtonia landing page.
// No dependencies. Respects prefers-reduced-motion.
(function () {
  var canvas = document.getElementById('stars');
  if (!canvas) return;
  var ctx = canvas.getContext('2d');
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  var stars = [];
  var w = 0, h = 0, dpr = Math.min(window.devicePixelRatio || 1, 2);

  function resize() {
    w = window.innerWidth;
    h = window.innerHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    canvas.style.width = w + 'px';
    canvas.style.height = h + 'px';
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    seed();
  }

  function seed() {
    var count = Math.round((w * h) / 6000);
    stars = [];
    for (var i = 0; i < count; i++) {
      var layer = Math.random();              // 0..1 depth
      stars.push({
        x: Math.random() * w,
        y: Math.random() * h,
        r: 0.4 + layer * 1.6,
        speed: 4 + layer * 22,                 // px/sec, far stars slower
        a: 0.25 + layer * 0.65
      });
    }
  }

  var last = 0;
  function frame(t) {
    var dt = last ? (t - last) / 1000 : 0;
    last = t;
    ctx.clearRect(0, 0, w, h);
    for (var i = 0; i < stars.length; i++) {
      var s = stars[i];
      s.x -= s.speed * dt;
      if (s.x < -2) { s.x = w + 2; s.y = Math.random() * h; }
      ctx.globalAlpha = s.a;
      ctx.fillStyle = s.r > 1.4 ? '#8effb8' : '#ffffff';
      ctx.beginPath();
      ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1;
    requestAnimationFrame(frame);
  }

  resize();
  window.addEventListener('resize', resize);

  if (reduce) {
    // Draw one static field, no animation.
    ctx.clearRect(0, 0, w, h);
    for (var i = 0; i < stars.length; i++) {
      var s = stars[i];
      ctx.globalAlpha = s.a;
      ctx.fillStyle = s.r > 1.4 ? '#8effb8' : '#ffffff';
      ctx.beginPath();
      ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2);
      ctx.fill();
    }
  } else {
    requestAnimationFrame(frame);
  }
})();
