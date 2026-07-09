// Lightweight parallax starfield + drifting wireframe asteroids for the
// Newtonia landing page. No dependencies. Respects prefers-reduced-motion.
(function () {
  var canvas = document.getElementById('stars');
  if (!canvas) return;
  var ctx = canvas.getContext('2d');
  var reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  var stars = [];
  var asteroids = [];
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
    seedAsteroids();
  }

  // 9-vertex irregular polygons, like asteroid.cpp's shape generation.
  function makeAsteroidShape(radius) {
    var pts = [];
    for (var v = 0; v < 9; v++) {
      var ang = (v / 9) * Math.PI * 2;
      var r = radius * (0.75 + Math.random() * 0.45);
      pts.push([Math.cos(ang) * r, Math.sin(ang) * r]);
    }
    return pts;
  }

  function seedAsteroids() {
    var count = Math.max(2, Math.round(w / 550));
    asteroids = [];
    for (var i = 0; i < count; i++) {
      var radius = 14 + Math.random() * 26;
      asteroids.push({
        x: Math.random() * w,
        y: Math.random() * h,
        pts: makeAsteroidShape(radius),
        r: radius,
        vx: -(6 + Math.random() * 14),
        vy: (Math.random() - 0.5) * 8,
        rot: Math.random() * Math.PI * 2,
        vrot: (Math.random() - 0.5) * 0.5,
        a: 0.14 + Math.random() * 0.14
      });
    }
  }

  function drawStar(s) {
    ctx.globalAlpha = s.a;
    ctx.fillStyle = s.r > 1.4 ? '#8fa8ff' : '#ffffff';
    ctx.beginPath();
    ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2);
    ctx.fill();
  }

  function drawAsteroid(o) {
    ctx.save();
    ctx.translate(o.x, o.y);
    ctx.rotate(o.rot);
    ctx.globalAlpha = o.a;
    ctx.strokeStyle = '#dfe6ff';
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(o.pts[0][0], o.pts[0][1]);
    for (var v = 1; v < o.pts.length; v++) ctx.lineTo(o.pts[v][0], o.pts[v][1]);
    ctx.closePath();
    ctx.stroke();
    ctx.restore();
  }

  function drawAll() {
    ctx.clearRect(0, 0, w, h);
    for (var i = 0; i < stars.length; i++) drawStar(stars[i]);
    for (var j = 0; j < asteroids.length; j++) drawAsteroid(asteroids[j]);
    ctx.globalAlpha = 1;
  }

  var last = 0;
  function frame(t) {
    var dt = last ? (t - last) / 1000 : 0;
    last = t;
    for (var i = 0; i < stars.length; i++) {
      var s = stars[i];
      s.x -= s.speed * dt;
      if (s.x < -2) { s.x = w + 2; s.y = Math.random() * h; }
    }
    for (var j = 0; j < asteroids.length; j++) {
      var o = asteroids[j];
      o.x += o.vx * dt;
      o.y += o.vy * dt;
      o.rot += o.vrot * dt;
      // toroidal wrap, like WrappedPoint
      if (o.x < -o.r * 2) o.x = w + o.r * 2;
      if (o.y < -o.r * 2) o.y = h + o.r * 2;
      if (o.y > h + o.r * 2) o.y = -o.r * 2;
    }
    drawAll();
    requestAnimationFrame(frame);
  }

  resize();
  window.addEventListener('resize', resize);

  if (reduce) {
    drawAll();                                  // one static field, no animation
  } else {
    requestAnimationFrame(frame);
  }
})();
