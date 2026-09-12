# Antey compartment reference contract

The production Project 949A compartment contract uses stable semantic IDs `compartment.01` through `compartment.10`. The functional names and ordering come from the supplied longitudinal-section reference. Longitudinal pressure-bulkhead positions are scaled from an undimensioned drawing and are therefore recorded as `REFERENCE_DERIVED_APPROXIMATE` with a nominal +/-1 m tolerance rather than claimed as shipyard dimensions.

| ID | Reference role | Authoring X range, m | Runtime intent |
| --- | --- | ---: | --- |
| `compartment.01` | First / bow torpedo compartment | +46.5 .. +73.0 | torpedo/rocket weapons and handling |
| `compartment.02` | Second / central compartment | +34.5 .. +46.5 | central-post support / battery group |
| `compartment.03` | Third compartment | +23.5 .. +34.5 | deliberately unspecified by supplied legend |
| `compartment.04` | Fourth / habitability compartment | +6.5 .. +23.5 | habitability |
| `compartment.05` | Fifth / auxiliary machinery | -0.5 .. +6.5 | auxiliary machinery |
| `compartment.06` | Sixth / auxiliary machinery | -7.5 .. -0.5 | auxiliary machinery |
| `compartment.07` | Seventh / reactor compartment | -18.5 .. -7.5 | reactor plant |
| `compartment.08` | Eighth / turbine compartment | -32.5 .. -18.5 | forward turbine plant / forward main switchboard |
| `compartment.09` | Ninth / turbine compartment | -46.0 .. -32.5 | aft turbine plant / aft main switchboard |
| `compartment.10` | Tenth / propulsion electric motor | -56.0 .. -46.0 | propulsion electric motor |

P-700 launch containers, the MGK-540 bow-array allocation, VVD bottles, sail equipment, shafting and steering gear are not pressure-hull compartments. In particular, the region aft of approximately X=-56 m remains an external aft shaft/steering region rather than extending `compartment.10` to the propellers.

`Content/submarines/Antey/Antey.compartments.json` is the reviewable reference-derived source for this mapping. The corresponding records embedded in `Antey.authoring.json` carry stable semantic IDs, Russian display names, functional roles, role provenance, system tags and matching X ranges. `ProductionAnteyAssetDefinition` exposes that functional metadata while Simulation remains authoritative for damage, flooding, fire, atmosphere, crew, access boundaries and equipment state.
