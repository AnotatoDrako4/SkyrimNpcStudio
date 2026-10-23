/* Animation Intelligence Lab - offline report behaviour.
 *
 * Security rules this file obeys without exception:
 *   - every value from the scan reaches the DOM through textContent, never
 *     innerHTML, so a mod name containing markup or a <script> tag is shown
 *     literally and cannot execute;
 *   - no network access, no third-party library, no eval, no new Function;
 *   - file paths are displayed as text only. Nothing is ever opened or run.
 */
(function () {
  "use strict";

  var dataNode = document.getElementById("report-data");
  var DATA = JSON.parse(dataNode.textContent);

  function el(tag, opts, kids) {
    var node = document.createElement(tag);
    opts = opts || {};
    if (opts.className) node.className = opts.className;
    if (opts.text !== undefined && opts.text !== null) node.textContent = String(opts.text);
    if (opts.attrs) {
      Object.keys(opts.attrs).forEach(function (k) {
        node.setAttribute(k, String(opts.attrs[k]));
      });
    }
    (kids || []).forEach(function (kid) {
      if (kid) node.appendChild(kid);
    });
    return node;
  }

  function pill(text, cls) {
    return el("span", { className: "pill " + cls, text: text });
  }

  function card(label, value) {
    return el("div", { className: "card" }, [
      el("div", { className: "n", text: value }),
      el("div", { className: "k", text: label })
    ]);
  }

  function bytes(n) {
    if (n === undefined || n === null) return "";
    if (n < 1024) return n + " B";
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB";
    return (n / (1024 * 1024)).toFixed(2) + " MiB";
  }

  function mount(id, node) {
    var host = document.getElementById(id);
    if (host && node) host.appendChild(node);
  }

  /* ---------- summary ---------- */

  function renderSummary() {
    var s = DATA.summary || {};
    var p = DATA.profile || {};
    mount("summary", el("div", { className: "cards" }, [
      card("enabled mods", p.enabledMods || 0),
      card("loose .hkx paths", p.countedLooseAnimations || 0),
      card("file conflicts", s.loosePathConflicts || 0),
      card("OAR candidates", s.oarCandidates || 0),
      card("OAR overlaps across mods", s.oarCrossModOverlaps || 0),
      card("OAR overlaps within one mod",
           (s.oarPotentialOverlaps || 0) - (s.oarCrossModOverlaps || 0)),
      card("animations only in archives", s.archiveOnlyAnimationPaths || 0),
      card("DAR rule sets", s.darRuleSets || 0),
      card("behaviour files", s.behaviorOutputs || 0),
      card("unknown / unverified", s.unknowns || 0)
    ]));
  }

  /* ---------- issue table ---------- */

  function providerTable(providers) {
    var head = el("tr", {}, [
      el("th", { text: "role" }),
      el("th", { text: "mod" }),
      el("th", { text: "priority" }),
      el("th", { text: "path" }),
      el("th", { text: "size" }),
      el("th", { text: "sha-256" })
    ]);
    var body = el("tbody", {}, providers.map(function (pr) {
      return el("tr", {}, [
        el("td", { text: pr.role || "" }),
        el("td", { text: pr.mod || "" }),
        el("td", { text: pr.priority === undefined ? "" : pr.priority }),
        el("td", { className: "path", text: pr.path || "" }),
        el("td", { text: bytes(pr.bytes) }),
        el("td", { className: "path", text: pr.sha256 ? pr.sha256.slice(0, 16) + "..." : "" })
      ]);
    }));
    return el("table", { className: "providers" }, [el("thead", {}, [head]), body]);
  }

  function detailRow(issue, columns) {
    var dl = el("dl", {}, [
      el("dt", { text: "what was observed" }),
      el("dd", { text: issue.whatWasObserved || "" }),
      el("dt", { text: "why it matters" }),
      el("dd", { text: issue.whyItMatters || "" }),
      el("dt", { text: "safe action" }),
      el("dd", { text: issue.action || "" }),
      el("dt", { text: "runtime verification" }),
      el("dd", { text: issue.runtimeVerification || "" }),
      el("dt", { text: "source reference" }),
      el("dd", { text: issue.sourceReference || "" }),
      el("dt", { text: "issue id" }),
      el("dd", { text: issue.id || "" })
    ]);
    var cell = el("td", { attrs: { colspan: columns } }, [dl]);
    if (issue.providers && issue.providers.length) {
      cell.appendChild(providerTable(issue.providers));
    }
    var row = el("tr", { className: "detail" }, [cell]);
    row.hidden = true;
    return row;
  }

  function unique(list) {
    var seen = {};
    var out = [];
    list.forEach(function (v) {
      if (!v || seen[v]) return;
      seen[v] = true;
      out.push(v);
    });
    return out.sort();
  }

  function flatten(lists) {
    return lists.reduce(function (acc, l) { return acc.concat(l); }, []);
  }

  function fillSelect(select, values) {
    values.forEach(function (v) {
      select.appendChild(el("option", { text: v, attrs: { value: v } }));
    });
  }

  function renderIssues() {
    // The payload is already ordered most-severe first by engine/issues.py
    // sort_issues. A former re-sort here used a falsy-zero fallback that ranked
    // "high" (rank 0) as unknown and pushed every high row to the bottom.
    var all = (DATA.issues || []).slice();

    var search = document.getElementById("f-search");
    var sev = document.getElementById("f-severity");
    var cat = document.getElementById("f-category");
    var conf = document.getElementById("f-confidence");
    var mod = document.getElementById("f-mod");
    var counter = document.getElementById("issue-count");
    var tbody = document.getElementById("issue-body");

    fillSelect(cat, unique(all.map(function (i) { return i.category; })));
    fillSelect(mod, unique(flatten(all.map(function (i) {
      return (i.providers || []).map(function (p) { return p.mod; });
    }))));

    function matches(issue) {
      if (sev.value && issue.severity !== sev.value) return false;
      if (cat.value && issue.category !== cat.value) return false;
      if (conf.value && issue.confidence !== conf.value) return false;
      if (mod.value) {
        var mods = (issue.providers || []).map(function (p) { return p.mod; });
        if (mods.indexOf(mod.value) === -1) return false;
      }
      var q = search.value.trim().toLowerCase();
      if (!q) return true;
      var hay = [issue.subject, issue.category, issue.whatWasObserved, issue.action]
        .concat((issue.providers || []).map(function (p) { return p.mod + " " + p.path; }))
        .join(" ")
        .toLowerCase();
      return hay.indexOf(q) !== -1;
    }

    function draw() {
      while (tbody.firstChild) tbody.removeChild(tbody.firstChild);
      var shown = 0;
      all.forEach(function (issue) {
        if (!matches(issue)) return;
        shown += 1;
        var head = el("tr", { className: "row-head" }, [
          el("td", {}, [pill(issue.severity, "sev-" + issue.severity)]),
          el("td", {}, [pill(issue.confidence, "conf-" + issue.confidence)]),
          el("td", { text: issue.category }),
          el("td", { className: "path", text: issue.subject }),
          el("td", { text: (issue.providers || []).length })
        ]);
        var detail = detailRow(issue, 5);
        head.addEventListener("click", function () {
          detail.hidden = !detail.hidden;
        });
        tbody.appendChild(head);
        tbody.appendChild(detail);
      });
      counter.textContent = shown + " of " + all.length + " findings shown";
      if (shown === 0) {
        tbody.appendChild(el("tr", {}, [
          el("td", { attrs: { colspan: 5 }, text: "No finding matches these filters." })
        ]));
      }
    }

    [search, sev, cat, conf, mod].forEach(function (control) {
      control.addEventListener("input", draw);
      control.addEventListener("change", draw);
    });
    document.getElementById("f-reset").addEventListener("click", function () {
      search.value = "";
      [sev, cat, conf, mod].forEach(function (s) { s.value = ""; });
      draw();
    });
    draw();
  }

  /* ---------- file conflict panel ---------- */

  function renderConflicts() {
    var rows = DATA.fileConflicts || [];
    if (!rows.length) {
      mount("conflicts", el("p", {
        className: "empty",
        text: "No loose animation path is supplied by two or more enabled mods in this profile."
      }));
      return;
    }
    mount("conflicts", el("table", {}, [
      el("thead", {}, [el("tr", {}, [
        el("th", { text: "path" }),
        el("th", { text: "kind" }),
        el("th", { text: "winner (MO2 priority)" }),
        el("th", { text: "overridden" }),
        el("th", { text: "identical bytes" })
      ])]),
      el("tbody", {}, rows.map(function (c) {
        return el("tr", {}, [
          el("td", { className: "path", text: c.path }),
          el("td", { text: c.kind }),
          el("td", { text: c.winner.mod + " (" + c.winner.priority + ")" }),
          el("td", {
            text: c.overridden.map(function (o) {
              return o.mod + " (" + o.priority + ")";
            }).join(", ")
          }),
          el("td", { text: c.identicalContent ? "yes - harmless" : "no" })
        ]);
      }))
    ]));
    mount("conflicts", el("p", {
      className: "hint",
      text: "MO2 loose-file winner is a file fact, not a proven in-game result. " +
            "BSA archives were not opened."
    }));
  }

  /* ---------- OAR overlap panel ---------- */

  function renderOverlaps() {
    var rows = DATA.oarOverlaps || [];
    if (!rows.length) {
      mount("overlaps", el("p", {
        className: "empty",
        text: "No animation path is claimed by two or more active OAR submods."
      }));
      return;
    }
    mount("overlaps", el("table", {}, [
      el("thead", {}, [el("tr", {}, [
        el("th", { text: "replacement target" }),
        el("th", { text: "scope" }),
        el("th", { text: "candidates in OAR priority order" }),
        el("th", { text: "runtime winner" })
      ])]),
      el("tbody", {}, rows.map(function (o) {
        var scope = o.crossMod
          ? pill((o.providerMods || []).length + " mods", "sev-medium")
          : pill("same mod", "sev-info");
        var list = el("ul", { className: "plain" }, o.candidates.map(function (c) {
          return el("li", {
            text: c.name + "  [priority " + c.priority + ", " + c.classification +
                  ", " + c.conditionCount + " condition(s), mod: " + c.providerMod + "]"
          });
        }));
        return el("tr", {}, [
          el("td", { className: "path", text: o.target }),
          el("td", {}, [scope]),
          el("td", {}, [list]),
          el("td", {}, [pill("UNKNOWN", "sev-medium")])
        ]);
      }))
    ]));
    mount("overlaps", el("p", {
      className: "hint",
      text: "Cross-mod competition is listed first; overlaps within one mod are usually " +
            "the author's own random variants. The runtime winner is deliberately left " +
            "UNKNOWN. OAR evaluates each " +
            "candidate's conditions in priority order against live game state (race, " +
            "equipment, weather, combat, random rolls), and the in-game OAR menu can " +
            "disable a submod. None of that can be read from files."
    }));
  }

  /* ---------- profile comparison ---------- */

  function renderComparison() {
    var cmp = DATA.comparison;
    var host = document.getElementById("comparison-section");
    if (!cmp) {
      if (host) host.hidden = true;
      return;
    }
    var box = el("div", {}, [
      el("div", { className: "cards" }, [
        card("only in " + cmp.profileA, (cmp.modsOnlyInA || []).length),
        card("only in " + cmp.profileB, (cmp.modsOnlyInB || []).length),
        card("shared enabled mods", cmp.sharedMods || 0),
        card("conflicts only in " + cmp.profileA, (cmp.conflictsOnlyInA || []).length),
        card("conflicts only in " + cmp.profileB, (cmp.conflictsOnlyInB || []).length),
        card("winner changed", (cmp.winnerChanged || []).length)
      ])
    ]);
    if ((cmp.winnerChanged || []).length) {
      box.appendChild(el("table", {}, [
        el("thead", {}, [el("tr", {}, [
          el("th", { text: "path" }),
          el("th", { text: "winner in " + cmp.profileA }),
          el("th", { text: "winner in " + cmp.profileB })
        ])]),
        el("tbody", {}, cmp.winnerChanged.map(function (w) {
          return el("tr", {}, [
            el("td", { className: "path", text: w.path }),
            el("td", { text: w.winnerA }),
            el("td", { text: w.winnerB })
          ]);
        }))
      ]));
    }
    box.appendChild(el("p", {
      className: "hint",
      text: "Each profile was scanned into its own snapshot with no shared cache. " +
            "Fingerprints: " + cmp.fingerprintA + " / " + cmp.fingerprintB
    }));
    mount("comparison", box);
  }

  /* ---------- recommendations and limits ---------- */

  function renderList(hostId, items, emptyText) {
    if (!items || !items.length) {
      mount(hostId, el("p", { className: "empty", text: emptyText }));
      return;
    }
    mount(hostId, el("ul", { className: "plain" }, items.map(function (t) {
      return el("li", { text: typeof t === "string" ? t : JSON.stringify(t) });
    })));
  }

  function renderRecommendations() {
    var recs = DATA.recommendations || [];
    if (!recs.length) {
      mount("recommendations", el("p", {
        className: "empty", text: "No action is recommended from this scan."
      }));
      return;
    }
    mount("recommendations", el("table", {}, [
      el("thead", {}, [el("tr", {}, [
        el("th", { text: "priority" }),
        el("th", { text: "recommended manual step" }),
        el("th", { text: "because" }),
        el("th", { text: "evidence" })
      ])]),
      el("tbody", {}, recs.map(function (r, i) {
        return el("tr", {}, [
          el("td", { text: i + 1 }),
          el("td", { text: r.action }),
          el("td", { text: r.because }),
          el("td", { className: "path", text: (r.issueIds || []).join(", ") })
        ]);
      }))
    ]));
    mount("recommendations", el("p", {
      className: "hint",
      text: "Every step above is manual and reversible. This tool applied none of them."
    }));
  }

  /* ---------- JSON export ---------- */

  function wireExport() {
    var button = document.getElementById("export-json");
    if (!button) return;
    button.addEventListener("click", function () {
      var blob = new Blob([JSON.stringify(DATA, null, 2)], { type: "application/json" });
      var url = URL.createObjectURL(blob);
      var link = el("a", { attrs: { href: url, download: "animation-intelligence.json" } });
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      URL.revokeObjectURL(url);
    });
  }

  renderSummary();
  renderIssues();
  renderConflicts();
  renderOverlaps();
  renderComparison();
  renderRecommendations();
  renderList("limitations", DATA.limitations, "No limitation was recorded.");
  renderList("evidence", (DATA.evidence || []).map(function (e) {
    return e.claim + "  ->  " + e.source;
  }), "No evidence entry was recorded.");
  wireExport();
})();
