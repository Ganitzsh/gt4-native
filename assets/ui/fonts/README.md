# UI fonts

The host UI's fonts (docs/modern/MENU_DESIGN.md 5.2; PLAN.md E5), all SIL Open Font License 1.1, kept byte for byte
as their projects publish them: gt4_host embeds them (`cmake/Host.cmake`, `cmake/EmbedFile.cmake`), so a release
needs this folder only to build. Their licence travels in `THIRD_PARTY_NOTICES.md`; each project's own file is here.
Do not subset, convert or rename them: that would make a Modified Version under the OFL.

| File | Use | From | SHA-256 |
|---|---|---|---|
| `SairaSemiExpanded-BoldItalic.ttf` | titles, section names | Omnibus-Type/Saira `Saira/fonts/ttf/`, commit `1916f2a5` | `fc7541744a7087de41ac555869d11432667ed06e3a33183ca7c3ae601e754423` |
| `SairaSemiExpanded-Regular.ttf` | rows | the same | `36d301eb6701d9f3236bed97315e82ed790ca6a405516e2d974b809695ef701b` |
| `Saira-Regular.ttf` | help | the same | `7b7ca56a5bd93823bbbc9c815fd1569cfd1d78627d319b11b6ca95d12af191ea` |
| `LibreCaslonText-Regular.ttf` | accent | thundernixon/Libre-Caslon `fonts/static/`, commit `52200a76` | `be0839d01507ee8767eca5ce1a4ea31c7cf067d00327e37683455176539e809c` |
| `OFL-Saira.txt`, `OFL-LibreCaslonText.txt` | the projects' licence files (`OFL.txt` at those commits) | | |

These are the commits Google Fonts names as the families' sources (google/fonts `ofl/saira`, `ofl/librecaslontext`
`upstream_info.md`); Google Fonts itself now ships Saira only as a variable font.
