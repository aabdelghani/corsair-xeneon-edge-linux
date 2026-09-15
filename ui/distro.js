// EdgeLine UI: which distribution's theme family matches this machine.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app ships Ubuntu, Fedora and NixOS themes, and by default wears the one
// that matches the machine. Read from /etc/os-release, which every current
// distribution provides. ID is tried before ID_LIKE, so a derivative matches
// its parent: Mint and Pop!_OS list "ubuntu debian", Nobara, Rocky and Alma
// list "rhel centos fedora". Anything unrecognised gets the Ubuntu family,
// which is the design's default.
//
// Pure except for the file read, so the mapping is tested against sample
// os-release contents. EDGELINE_OS_RELEASE points it at another file, which
// is how a NixOS machine is tested from one that is not.
'use strict';

const fs = require('fs');

const FAMILY_OF = {
  ubuntu: 'ubuntu', debian: 'ubuntu',
  fedora: 'fedora', rhel: 'fedora', centos: 'fedora',
  nixos: 'nixos',
};

function distroFromOsRelease(text) {
  const field = (key) => {
    const m = new RegExp(`^${key}=(.*)$`, 'm').exec(text || '');
    return m ? m[1].trim().replace(/^["']|["']$/g, '').toLowerCase() : '';
  };
  const id = field('ID');
  if (FAMILY_OF[id]) return FAMILY_OF[id];
  for (const like of field('ID_LIKE').split(/\s+/).filter(Boolean))
    if (FAMILY_OF[like]) return FAMILY_OF[like];
  return 'ubuntu';
}

function detectDistro(file = process.env.EDGELINE_OS_RELEASE || '/etc/os-release') {
  try {
    return distroFromOsRelease(fs.readFileSync(file, 'utf8'));
  } catch {
    return 'ubuntu';
  }
}

module.exports = { distroFromOsRelease, detectDistro };
