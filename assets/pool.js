// Awase: the hero animation and the small-screen menu.
// Copyright 2026, Ian Dumych, idumych@gmail.com.
// SPDX-License-Identifier: MIT
//
// The hero draws a pool at work: packages in dependency order, volunteers
// at the edges sending builds to them, and a seal on each package once two
// builds of it have matched. A package's dependents become buildable only
// after it is sealed. Now and then two builds disagree and the package is
// handed out again.

(function () {
	"use strict";

	var toggle = document.querySelector(".menu-toggle");
	var nav = document.getElementById("nav");
	if (toggle && nav) {
		toggle.addEventListener("click", function () {
			var open = nav.classList.toggle("open");
			toggle.setAttribute("aria-expanded", open ? "true" : "false");
		});
		nav.addEventListener("click", function (e) {
			if (e.target.tagName === "A") {
				nav.classList.remove("open");
				toggle.setAttribute("aria-expanded", "false");
			}
		});
	}

	var canvas = document.getElementById("loom");
	if (!canvas || !canvas.getContext)
		return;
	var ctx = canvas.getContext("2d");
	var still = window.matchMedia
		&& window.matchMedia("(prefers-reduced-motion: reduce)").matches;

	var FIRE = "255,138,61", WASHI = "239,233,221", GOLD = "242,193,78";
	var SEAL = "#d8432a";
	var LAYERS = [
		["binutils", "gcc", "zlib"],
		["openssl", "libpng", "freetype", "libffi"],
		["glib2", "curl", "harfbuzz", "python3", "sqlite"],
		["libsdl2", "gtk3", "ffmpeg", "llvm"],
		["qt6_base", "sdl2_mixer", "mpv"],
	];

	var W, H, dpr, nodes, volunteers, threads, nextDispatch, doneAt;

	function rand(a, b) { return a + Math.random() * (b - a); }

	function layout() {
		dpr = Math.min(window.devicePixelRatio || 1, 2);
		W = canvas.clientWidth;
		H = canvas.clientHeight;
		canvas.width = Math.round(W * dpr);
		canvas.height = Math.round(H * dpr);
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	}

	function build() {
		var narrow = W < 720;
		var x0 = narrow ? W * 0.08 : W * (W >= 1100 ? 0.6 : 0.5), x1 = W * 0.95;
		var y0 = H * 0.16, y1 = H * 0.86;
		nodes = [];
		LAYERS.forEach(function (names, li) {
			var x = x0 + (x1 - x0) * li / (LAYERS.length - 1);
			names.forEach(function (name, i) {
				var y = y0 + (y1 - y0) * (i + 0.5) / names.length;
				nodes.push({
					name: name, layer: li,
					x: x + rand(-18, 18), y: y + rand(-22, 22),
					deps: [], state: "waiting", sent: [], arrived: 0,
					sealT: 0, flashT: 0
				});
			});
		});
		nodes.forEach(function (n) {
			if (n.layer === 0)
				return;
			var prev = nodes.filter(function (m) { return m.layer === n.layer - 1; });
			prev.sort(function (a, b) {
				return Math.abs(a.y - n.y) - Math.abs(b.y - n.y);
			});
			n.deps.push(prev[0]);
			if (prev[1] && Math.random() < 0.6)
				n.deps.push(prev[1]);
		});
		volunteers = [];
		for (var i = 0; i < 8; i++) {
			var t = i / 7;
			volunteers.push(i % 2
				? { x: W + 8, y: H * (0.1 + 0.8 * t) }
				: { x: x0 + (W - x0) * t, y: H + 8 });
		}
		threads = [];
		doneAt = 0;
		nextDispatch = 0;
	}

	function ready(n) {
		return n.state === "waiting" && n.deps.every(function (d) {
			return d.state === "sealed";
		});
	}

	function dispatch(now) {
		var open = nodes.filter(function (n) {
			return (ready(n) || n.state === "building") && n.sent.length < 2;
		});
		if (!open.length)
			return;
		var n = open[Math.floor(Math.random() * open.length)];
		var choices = volunteers.filter(function (v) {
			return n.sent.indexOf(v) < 0;
		});
		var v = choices[Math.floor(Math.random() * choices.length)];
		n.state = "building";
		n.sent.push(v);
		var cx = (v.x + n.x) / 2 + rand(-80, 80), cy = (v.y + n.y) / 2 + rand(-80, 80);
		threads.push({ v: v, n: n, cx: cx, cy: cy, t0: now, dur: rand(1400, 3600) });
	}

	function point(th, t) {
		var u = 1 - t;
		return [u * u * th.v.x + 2 * u * t * th.cx + t * t * th.n.x,
			u * u * th.v.y + 2 * u * t * th.cy + t * t * th.n.y];
	}

	function arrive(n, now) {
		n.arrived++;
		if (n.arrived < 2)
			return;
		if (Math.random() < 0.1) {
			n.flashT = now;
			n.state = "waiting";
			n.sent = [];
			n.arrived = 0;
		} else {
			n.state = "sealed";
			n.sealT = now;
		}
	}

	function drawSeal(x, y, s) {
		var r = 11 * s;
		ctx.save();
		ctx.translate(x, y);
		ctx.rotate(-0.1);
		ctx.fillStyle = SEAL;
		ctx.beginPath();
		if (ctx.roundRect)
			ctx.roundRect(-r, -r, 2 * r, 2 * r, 4 * s);
		else
			ctx.rect(-r, -r, 2 * r, 2 * r);
		ctx.fill();
		ctx.fillStyle = "#fff4ea";
		ctx.font = "800 " + Math.round(14 * s) + "px 'Shippori Mincho', serif";
		ctx.textAlign = "center";
		ctx.textBaseline = "middle";
		ctx.fillText("合", 0, 1 * s);
		ctx.restore();
	}

	function frame(now) {
		ctx.clearRect(0, 0, W, H);

		if (!still) {
			if (now > nextDispatch) {
				dispatch(now);
				nextDispatch = now + rand(140, 420);
			}
			threads = threads.filter(function (th) {
				if (now - th.t0 >= th.dur) {
					arrive(th.n, now);
					return false;
				}
				return true;
			});
			var all = nodes.every(function (n) { return n.state === "sealed"; });
			if (all && !doneAt)
				doneAt = now;
			if (doneAt && now - doneAt > 2600)
				build();
		}

		// Dependencies.
		nodes.forEach(function (n) {
			n.deps.forEach(function (d) {
				var lit = d.state === "sealed" && n.state === "sealed";
				ctx.strokeStyle = lit ? "rgba(" + FIRE + ",.32)" : "rgba(" + WASHI + ",.07)";
				ctx.lineWidth = 1;
				ctx.beginPath();
				ctx.moveTo(d.x, d.y);
				ctx.bezierCurveTo((d.x + n.x) / 2, d.y, (d.x + n.x) / 2, n.y, n.x, n.y);
				ctx.stroke();
			});
		});

		// Builds in flight.
		threads.forEach(function (th) {
			var t = Math.min(1, (now - th.t0) / th.dur);
			var steps = 18, tail = 0.22;
			for (var i = 0; i < steps; i++) {
				var a = Math.max(0, t - tail * (1 - i / steps));
				var b = Math.max(0, t - tail * (1 - (i + 1) / steps));
				var p = point(th, a), q = point(th, b);
				ctx.strokeStyle = "rgba(" + FIRE + "," + (0.55 * i / steps) + ")";
				ctx.lineWidth = 1.6;
				ctx.beginPath();
				ctx.moveTo(p[0], p[1]);
				ctx.lineTo(q[0], q[1]);
				ctx.stroke();
			}
			var h = point(th, t);
			ctx.fillStyle = "rgba(" + FIRE + ",.95)";
			ctx.beginPath();
			ctx.arc(h[0], h[1], 2.2, 0, Math.PI * 2);
			ctx.fill();
		});

		// Packages.
		ctx.font = "400 11px 'IBM Plex Mono', monospace";
		ctx.textAlign = "center";
		ctx.textBaseline = "top";
		nodes.forEach(function (n) {
			if (n.state === "sealed") {
				var k = still ? 1 : Math.min(1, (now - n.sealT) / 260);
				var s = k < 1 ? 1.5 - 0.5 * k : 1;
				drawSeal(n.x, n.y, s);
			} else {
				var flash = n.flashT && now - n.flashT < 900;
				var on = n.state === "building" || ready(n);
				ctx.strokeStyle = flash ? "rgba(" + GOLD + ",.95)"
					: "rgba(" + WASHI + "," + (on ? 0.75 : 0.25) + ")";
				ctx.lineWidth = flash ? 2 : 1.4;
				ctx.beginPath();
				ctx.arc(n.x, n.y, flash ? 8 : 6, 0, Math.PI * 2);
				ctx.stroke();
				if (n.arrived === 1) {
					ctx.fillStyle = "rgba(" + FIRE + ",.8)";
					ctx.beginPath();
					ctx.arc(n.x, n.y, 3, 0, Math.PI * 2);
					ctx.fill();
				}
			}
			ctx.fillStyle = "rgba(" + WASHI + "," + (n.state === "sealed" ? 0.6 : 0.32) + ")";
			ctx.fillText(n.name, n.x, n.y + 15);
		});

		if (!still)
			requestAnimationFrame(frame);
	}

	function start() {
		layout();
		build();
		if (still) {
			// A pool partway through: the first layers sealed.
			nodes.forEach(function (n) {
				if (n.layer < 3)
					n.state = "sealed";
			});
			frame(0);
			return;
		}
		// Open partway through, so the first frame already shows the pool.
		var now = performance.now();
		nodes.forEach(function (n) {
			if (n.layer === 0) {
				n.state = "sealed";
				n.sealT = now - 1000;
			}
		});
		for (var i = 0; i < 6; i++)
			dispatch(now - rand(0, 1200));
		frame(now);
	}

	var resizeTimer;
	window.addEventListener("resize", function () {
		clearTimeout(resizeTimer);
		resizeTimer = setTimeout(function () {
			layout();
			build();
			if (still)
				start();
		}, 150);
	});

	start();
	// The seals use the web font; redraw a still picture once it's in.
	if (still && document.fonts && document.fonts.ready)
		document.fonts.ready.then(function () { frame(0); });
})();
