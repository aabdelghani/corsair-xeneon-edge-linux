// Edgeline: the Developer page.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two of the design's four cards describe things this build does not do. They
// are rendered as designed and marked, with the reason on the card, because a
// page that quietly drops the features it cannot deliver is how you end up
// trusting a mockup instead of the software.
'use strict';

function codeBlock(lines, firstBright) {
  return el('div', {
    class: 'mono',
    style: 'font-size:12px;line-height:2;color:var(--text3);background:var(--sunken);'
         + 'border-radius:8px;padding:12px 14px;word-break:break-all',
  }, ...lines.map((l, i) =>
    el('div', { style: firstBright && i > 0 ? 'color:var(--text4)' : '' }, l)));
}

function devCard(iconCls, title, note, body, extra) {
  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:11px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { style: 'font-size:15px' },
        icon(iconCls, 'margin-right:8px;color:var(--text4);font-size:13px'), title),
      note ? el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' }, note) : null),
    body,
    extra);
}

function iconSvg(size, radius) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('width', size);
  svg.setAttribute('height', size);
  svg.setAttribute('viewBox', '0 0 64 64');
  svg.setAttribute('fill', 'none');
  svg.style.cssText = 'display:block;flex:none';
  svg.innerHTML =
    `<rect width="64" height="64" rx="${radius}" fill="var(--accent)"></rect>`
    + '<rect x="12" y="26" width="40" height="12" rx="4" fill="var(--on-accent)"></rect>';
  return svg;
}

function packagingCard() {
  // Only one of these four exists. Presenting them as four equal options would
  // be a claim the project cannot back.
  const targets = [
    ['Flatpak', 'fa-solid fa-box', 'dev.edgeline.Ctl', false],
    ['.deb', 'fa-brands fa-ubuntu', 'built from packaging/', true],
    ['.rpm / COPR', 'fa-brands fa-fedora', 'not built', false],
    ['AUR', 'fa-brands fa-linux', 'not built', false],
  ];
  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:12px' },
    el('div', { class: 'card-kicker' }, 'PACKAGING'),
    el('div', { style: 'display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px' },
      ...targets.map(([name, ic, sub, built]) =>
        el('div', {
          style: 'border:1px solid var(--border2);border-radius:9px;padding:13px;'
               + 'display:flex;flex-direction:column;gap:5px'
               + (built ? '' : ';opacity:.5'),
        },
          el('div', { style: 'font-size:14px;color:var(--text)' },
            icon(ic, 'margin-right:7px;color:var(--text4);font-size:12px'), name),
          el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text4)' }, sub)))),
    el('div', {
      style: 'border-top:1px solid var(--border);padding-top:12px;display:flex;align-items:center;gap:12px;flex-wrap:wrap',
    },
      el('div', { style: 'font-size:13.5px;color:var(--text2)' }, 'Device access'),
      el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text4)' },
        '/usr/lib/udev/rules.d/60-corsair-xeneon.rules'),
      el('div', { style: 'margin-left:auto;font-size:12.5px;color:var(--text3)' },
        state.device.accessible ? 'hidraw reachable' : 'hidraw denied')),
    el('div', { class: 'card-sub' },
      'Picture control also needs your user in the i2c group. The package prints a '
      + 'reminder; it cannot add you to a group on your behalf.'));
}

PAGES.developer = (host) => {
  const sys = state.system;

  fill(host,
    pageHead('Developer', 'CLI · D-BUS · IMPORT · PACKAGING'),

    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:18px' },
      devCard('fa-solid fa-terminal', 'Command line', 'talks to the same agent', codeBlock([
        '$ edgeline status',
        '$ edgeline set brightness 40',
        '$ edgeline touch mode own-pointer',
        '$ edgeline touch --live 15',
      ])),
      devCard('fa-solid fa-diagram-project', 'Local socket', 'JSON, one per line', codeBlock([
        sys.socket || '/run/user/…/edgeline.sock',
        '{"id":1,"method":"ddc.set",',
        '  "params":{"code":16,"value":40}}',
        'events: ddc · touch · sensors',
      ], true),
        el('div', { class: 'why' }, icon('fa-solid fa-circle-info'),
          el('span', {}, 'A D-Bus interface (dev.edgeline.Ctl1) is designed but not implemented. '
            + 'The socket above is the working equivalent.')))),

    devCard('fa-solid fa-file-import', 'Import from iCUE', null,
      el('div', { class: 'card-sub' },
        'Would read a Windows iCUE export and map its picture settings onto profiles.'),
      el('div', { style: 'display:flex;align-items:center;gap:14px;flex-wrap:wrap' },
        el('button', { class: 'btn btn-small', disabled: true }, 'Choose file…'),
        el('div', { class: 'why', style: 'margin:0' }, icon('fa-solid fa-circle-info'),
          el('span', {}, 'Not implemented: the export format is undocumented and nobody has '
            + 'contributed a sample file to work from.')))),

    el('div', { style: 'height:14px' }),

    el('div', { class: 'card', style: 'padding:18px;display:flex;align-items:center;gap:22px;flex-wrap:wrap' },
      el('div', { style: 'display:flex;align-items:flex-end;gap:16px' },
        iconSvg(72, 18), iconSvg(48, 12), iconSvg(32, 8), iconSvg(22, 6)),
      el('div', { style: 'display:flex;flex-direction:column;gap:6px;min-width:0;flex:1' },
        el('div', { style: 'font-size:15px' }, 'App icon'),
        el('div', { class: 'card-sub' },
          'One SVG, drawn at 128 / 64 / 48 / 32 / 22. The corner radius tightens as it '
          + 'shrinks so the mark stays readable at tray size.'),
        el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text5)' },
          'Icon=dev.edgeline.Ctl · StartupWMClass=edgeline'))),

    el('div', { style: 'height:14px' }),
    packagingCard());
};
