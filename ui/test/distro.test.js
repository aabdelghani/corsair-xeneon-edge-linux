// EdgeLine UI: os-release contents distroFromOsRelease has to map correctly.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const assert = require('assert');
const { distroFromOsRelease } = require('../distro');

const cases = [
  ['Ubuntu', 'NAME="Ubuntu"\nID=ubuntu\nID_LIKE=debian\n', 'ubuntu'],
  ['Debian', 'ID=debian\n', 'ubuntu'],
  ['Linux Mint', 'ID=linuxmint\nID_LIKE="ubuntu debian"\n', 'ubuntu'],
  ['Pop!_OS', 'ID=pop\nID_LIKE="ubuntu debian"\n', 'ubuntu'],
  ['Fedora', 'NAME="Fedora Linux"\nID=fedora\n', 'fedora'],
  ['Nobara', 'ID=nobara\nID_LIKE="rhel centos fedora"\n', 'fedora'],
  ['Rocky Linux', 'ID="rocky"\nID_LIKE="rhel centos fedora"\n', 'fedora'],
  ['NixOS', 'NAME=NixOS\nID=nixos\n', 'nixos'],
  ['quoted NixOS id', "ID='nixos'\n", 'nixos'],
  ['Arch falls back to Ubuntu', 'ID=arch\n', 'ubuntu'],
  ['empty file falls back to Ubuntu', '', 'ubuntu'],
  ['ID wins over ID_LIKE', 'ID=fedora\nID_LIKE=debian\n', 'fedora'],
];

for (const [label, text, expected] of cases) {
  assert.strictEqual(distroFromOsRelease(text), expected, `${label}: expected ${expected}`);
  console.log(`  ok  ${label} -> ${expected}`);
}
console.log('distro: all cases pass');
