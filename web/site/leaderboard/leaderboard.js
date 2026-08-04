// Newtonia site leaderboard (LEADERBOARD.md "site leaderboard").
//
// Renders the daily snapshot the board worker's retention cron publishes
// (GET /site/leaderboard.json — top KEEP_N of every canonical season, solo
// and co-op, with the live board flagged). No dependencies; every piece of
// worker-supplied text lands via textContent, never innerHTML — names are
// player-controlled.
//
// ?board=beta points the page at the beta worker (dev/testing); ?players=
// and ?season= deep-link a specific board and are kept in sync with the
// pickers so the URL is always shareable.
(function () {
  var params = new URLSearchParams(location.search);
  var HOST = params.get('board') === 'beta'
      ? 'https://newtonia-board-beta.gfmcc.workers.dev'
      : 'https://newtonia-board.gfmcc.workers.dev';

  // net_identity.h platform tags (append-only enum).
  var PLATFORMS = ['', 'PC', 'STEAM', 'WEB', 'IOS', 'ANDROID', 'XBOX'];

  var elRows = document.getElementById('rows');
  var elBoard = document.getElementById('board');
  var elNote = document.getElementById('note');
  var elSeason = document.getElementById('season-select');
  var elUpdated = document.getElementById('updated');
  var btnSolo = document.getElementById('btn-solo');
  var btnCoop = document.getElementById('btn-coop');

  var snapshot = null;                 // {generated_at, boards:[...]}
  var players = params.get('players') === '2' ? 2 : 1;
  var season = params.get('season') || '';

  function boardsFor(p) {
    return (snapshot.boards || []).filter(function (b) {
      return b.players === p;
    });
  }

  function currentBoard() {
    var list = boardsFor(players);
    for (var i = 0; i < list.length; i++)
      if (list[i].season === season) return list[i];
    // Fall back to the live board, then the newest listed.
    for (var j = 0; j < list.length; j++)
      if (list[j].live) return list[j];
    return list[0] || null;
  }

  function note(text) {
    elBoard.hidden = true;
    elNote.hidden = false;
    elNote.textContent = text;
  }

  function fmtScore(n) {
    return Number(n).toLocaleString('en-US');
  }

  function fmtTime(ms) {
    var s = Math.floor(ms / 1000);
    var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
    function two(x) { return (x < 10 ? '0' : '') + x; }
    return h ? h + ':' + two(m) + ':' + two(r) : m + ':' + two(r);
  }

  function fmtDate(ms) {
    var d = new Date(ms);
    return d.getFullYear() + '-' +
        String(d.getMonth() + 1).padStart(2, '0') + '-' +
        String(d.getDate()).padStart(2, '0');
  }

  function syncUrl() {
    var q = new URLSearchParams(location.search);
    q.set('players', String(players));
    if (season) q.set('season', season);
    else q.delete('season');
    history.replaceState({}, '', location.pathname + '?' + q);
  }

  function render() {
    btnSolo.classList.toggle('on', players === 1);
    btnCoop.classList.toggle('on', players === 2);
    btnSolo.setAttribute('aria-pressed', String(players === 1));
    btnCoop.setAttribute('aria-pressed', String(players === 2));

    var b = currentBoard();
    // Season picker: the seasons that exist for this players count,
    // snapshot order (newest submission first).
    elSeason.innerHTML = '';
    boardsFor(players).forEach(function (s) {
      var opt = document.createElement('option');
      opt.value = s.season;
      opt.textContent = 'SEASON ' + s.season.toUpperCase() +
          (s.live ? ' — CURRENT' : '');
      elSeason.appendChild(opt);
    });
    if (!b) {
      // Keep the URL truthful even for an empty board (a CO-OP toggle with
      // no co-op scores yet): players updates, the stale season drops.
      season = '';
      syncUrl();
      note('NO SCORES YET — BE THE FIRST');
      return;
    }
    season = b.season;
    elSeason.value = season;
    syncUrl();

    elRows.innerHTML = '';
    b.rows.forEach(function (r) {
      var tr = document.createElement('tr');
      if (r.rank === 1) tr.className = 'top1';

      function td(cls, text) {
        var c = document.createElement('td');
        if (cls) c.className = cls;
        c.textContent = text;
        tr.appendChild(c);
        return c;
      }

      td('rank', '#' + r.rank);

      var pilot = document.createElement('td');
      var name = document.createElement('span');
      name.textContent = r.name || 'PILOT #' + r.rank;
      pilot.appendChild(name);
      if (r.verified && r.name) {
        var tick = document.createElement('span');
        tick.className = 'tick';
        tick.textContent = '✓';
        tick.title = 'Platform-verified name';
        pilot.appendChild(tick);
      }
      var platform = PLATFORMS[r.platform] || '';
      if (platform) {
        var badge = document.createElement('span');
        badge.className = 'badge';
        badge.textContent = platform;
        pilot.appendChild(badge);
      }
      tr.appendChild(pilot);

      td('score num', fmtScore(r.score));
      td('num', String(r.generation + 1));      // displayed level
      td('num', fmtTime(r.duration_ms));
      td('', fmtDate(r.date));

      var actions = document.createElement('td');
      if (r.has_replay) {
        var a = document.createElement('a');
        a.className = 'watch';
        // The web client downloads the blob from /replay/<season>/<run_id>
        // and jumps straight into replay playback (web/main.ts).
        a.href = '../play/?replay=' +
            encodeURIComponent(b.season) + '/' +
            encodeURIComponent(r.run_id) +
            (params.get('board') === 'beta' ? '&board=beta' : '');
        a.textContent = 'WATCH';
        a.title = 'Watch this run in your browser';
        actions.appendChild(a);
      }
      tr.appendChild(actions);
      elRows.appendChild(tr);
    });

    elNote.hidden = true;
    elBoard.hidden = false;
  }

  btnSolo.addEventListener('click', function () {
    players = 1; season = ''; if (snapshot) render();
  });
  btnCoop.addEventListener('click', function () {
    players = 2; season = ''; if (snapshot) render();
  });
  elSeason.addEventListener('change', function () {
    season = elSeason.value; if (snapshot) render();
  });

  fetch(HOST + '/site/leaderboard.json')
      .then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
      })
      .then(function (snap) {
        snapshot = snap;
        if (snap.generated_at)
          elUpdated.textContent = 'UPDATED ' + fmtDate(snap.generated_at);
        if (!snap.boards || !snap.boards.length)
          note('NO SCORES YET — BE THE FIRST');
        else
          render();
      })
      .catch(function (e) {
        note('LEADERBOARD UNAVAILABLE — TRY AGAIN LATER');
        console.warn('[newtonia] leaderboard fetch failed:', e);
      });
})();
