# Phoenix monster data

`src/trait_data.inc` is derived from https://github.com/phoenixffxi/Phoenix
at revision `5b1282e9277ebed506a9bd03490cbc6bc1cdd75b` (live branch),
under GPL-3.0; see Phoenix-MobDB.txt. The standalone DLL embeds this notice
and the upstream license. MobDB artwork retains its separate MIT notice.

Input: the standalone Phoenix-MobDB compiled SQLite database, using its
`configured_overlays` view. Generate with `tools/prepare_phoenix_traits.py`.
Input SQLite SHA256: `f4e89c8ae67ea4a8f398fab1f08d342a555e97ed29ae26a1905fee4c8cbf1a82`.
The compiled source reference applies configured era/Phoenix overlays;
their activation on the deployed server has not been independently verified.

The generator resolves explicit template display names, otherwise source labels
(using the matching script prefix for suffixed template variants), replaces
underscores with spaces, and limits names to the client's 24-byte field.
Client naming and scripted changes still require live verification.
Same-named spawns with differing traits require an exact index/name match.
Reserved slots without templates are omitted. Separate SQL instance records
are not imported; there is no fallback to the old generic monster database.

Only detection, aggression, linking and explicit physical/elemental damage
modifiers are exported. Elemental/status resistance ranks, status immunities,
general damage-taken modifiers, conditional scripts and level ranges are not
converted into these icons. The all-magic damage modifier is combined with
the element-specific modifier. Live level reporting remains independent.
