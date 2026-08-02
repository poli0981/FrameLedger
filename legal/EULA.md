# FrameLedger — End User License Agreement (EULA)

**Version:** 1.0-draft · **Effective:** {{RELEASE_DATE}}

> ⚠ Draft for review. Not legal advice. The developer should review before first public release.

This End User License Agreement ("Agreement") applies to **FrameLedger** ("the Software"), developed and published by **{{DEVELOPER_NAME}} (poli0981)** ("the Developer").

## 1. License

The Software is free and open-source software licensed to you under the **GNU General Public License, version 3.0 only (GPL-3.0-only)**. The full license text is provided in the `LICENSE` file and at <https://www.gnu.org/licenses/gpl-3.0.html>. Nothing in this Agreement limits or modifies your rights under the GPL-3.0. In case of conflict between this Agreement and the GPL-3.0, the GPL-3.0 prevails for everything it covers.

## 2. What the Software does

The Software records game performance data, hardware telemetry (temperatures, load, memory usage), and game metadata, and stores this data **locally on your device**. Details are described in the Privacy Policy and Disclaimer accompanying the Software.

## 2A. Code injection — your responsibility

To measure rendering settings accurately, the Software can load a component into a game process, **but only for games you have individually enabled**, after a consent prompt that explains the risk. The Software refuses to do so when it detects anti-cheat or anti-tamper software, and provides no means to override that refusal.

By enabling this feature for a game, you confirm that:

(a) you are responsible for complying with the terms of service of that game and its platform;

(b) you understand that anti-cheat systems may detect the Software and may warn, block, or **permanently ban** your account, and that this risk is yours alone;

(c) you understand that the Software's protective checks cannot cover every anti-cheat system and cannot guarantee your account's safety;

(d) the Software is intended for **offline and single-player play**, and any use with an online or competitive title is entirely at your own risk;

(e) the Developer has no ability to reverse a ban, recover lost progress, or intervene with any game publisher on your behalf, and accepts no liability for such outcomes.

A measurement mode requiring no injection is always available and is the default.

## 3. Acceptance

By clicking "Accept" in the first-run dialog or by using the Software, you confirm that you have read this Agreement, the Disclaimer, and the Privacy Policy.

## 4. Third-party components

The Software includes and interoperates with third-party components listed in `legal/THIRD_PARTY_NOTICES.md`, each under its own license. The optional **PawnIO** kernel driver is a separate third-party product installed by you at your discretion and governed by its own license and terms.

## 5. No warranty

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT, TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW AND AS STATED IN SECTIONS 15–17 OF THE GPL-3.0.

## 6. Limitation of liability

To the maximum extent permitted by applicable law, and as stated in the GPL-3.0, the Developer shall not be liable for any damages arising from the use or inability to use the Software, including but not limited to: data loss, hardware issues, interference with other software (including anti-cheat systems), or decisions made based on the measurements the Software reports.

## 7. Your responsibilities

You are responsible for: (a) complying with the terms of service of games and platforms you use alongside the Software; (b) deciding whether to install optional components such as PawnIO; (c) reviewing any bug report contents before submitting them.

## 8. Updates

The Software can check for updates via GitHub. Installing updates is always your choice. Updated legal documents will be presented for re-acceptance when their version changes.

## 9. Termination

Your rights under the GPL-3.0 continue as described in that license. You may stop using the Software at any time by uninstalling it; local data removal options are offered during uninstall.

---

Contact: {{CONTACT_EMAIL}} · Project: {{REPO_URL}}
