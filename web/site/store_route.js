// Which store THIS device can buy the game on — the one place the store
// ids, the device sniff, and the ANDROID_PUBLIC flag live. Consumed by
// every surface that offers a store link: the /join landing page, the
// site leaderboard's would-place CTA (leaderboard.js), and the web
// game's game-over banner (web/main.ts via shell.html). `make web`
// copies this file to the site root AND into /play/ so the off-domain
// itch.io deploy (which ships only /play) carries it too — a new
// consumer outside those trees needs its own cp in the Makefile's web
// target. Consumers guard on typeof NewtoniaStore, so a missing copy
// degrades to their markup default or no store link, never a throw.
//
// ANDROID_PUBLIC: flip to true when the Play listing is public (task
// #145 — NETPLAY.md's publish-day steps). While it's in closed testing
// a Play Store link dead-ends for non-testers, so Android surfaces
// offer no store button; /join additionally uses the flag to gate its
// install->auto-join button (the room code rides the install referrer).
var NewtoniaStore = (function () {
  var STEAM_APPID = '4536720';
  var ua = navigator.userAgent;
  return {
    ANDROID_PUBLIC: false,
    STEAM_APPID: STEAM_APPID,
    APP_STORE_URL: 'https://apps.apple.com/app/id6760685759',
    isIOS: /iPhone|iPad|iPod/i.test(ua) ||
        // iPadOS 13+ reports as desktop Safari, so sniff touch too.
        (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1),
    isAndroid: /Android/i.test(ua),
    // Steam's traffic report attributes external visits by UTM params on
    // the store URL (bare links land in "Other"); the other stores read
    // no UTM, which is why only this URL carries them.
    steamStoreUrl: function (utmSource, utmCampaign) {
      return 'https://store.steampowered.com/app/' + STEAM_APPID +
          '/Newtonia/?utm_source=' + utmSource +
          '&utm_medium=referral&utm_campaign=' + utmCampaign;
    },
    // Play reads campaign attribution — and /join's deferred deep-link
    // room code — from the install referrer, not from bare URL params.
    playStoreUrl: function (referrer) {
      return 'https://play.google.com/store/apps/details?id=org.newtonia' +
          (referrer ? '&referrer=' + encodeURIComponent(referrer) : '');
    }
  };
})();
