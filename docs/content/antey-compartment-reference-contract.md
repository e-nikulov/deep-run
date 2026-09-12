# Antey compartment reference contract

The production Project 949A compartment contract uses stable semantic IDs `compartment.01` through `compartment.10`. The functional names and ordering come from the supplied longitudinal-section reference. Longitudinal pressure-bulkhead positions are scaled from the undimensioned drawing against the accepted 154 m production length, rounded to reviewable half-metre/metre boundaries, and recorded as `REFERENCE_DERIVED_APPROXIMATE` with a nominal +/-1 m tolerance rather than claimed as shipyard dimensions.

| ID | Reference role | Authoring X range, m | Runtime intent |
| --- | --- | ---: | --- |
| `compartment.01` | First / bow torpedo compartment | +46.5 .. +66.5 | torpedo/rocket weapons and handling |
| `compartment.02` | Second / central compartment | +34.0 .. +46.5 | central-post support / battery group |
| `compartment.03` | Third compartment | +23.0 .. +34.0 | deliberately unspecified by supplied legend |
| `compartment.04` | Fourth / habitability compartment | +6.0 .. +23.0 | habitability |
| `compartment.05` | Fifth / auxiliary machinery | -1.0 .. +6.0 | auxiliary machinery |
| `compartment.06` | Sixth / auxiliary machinery | -8.0 .. -1.0 | auxiliary machinery |
| `compartment.07` | Seventh / reactor compartment | -19.0 .. -8.0 | reactor plant |
| `compartment.08` | Eighth / turbine compartment | -33.0 .. -19.0 | forward turbine plant / forward main switchboard |
| `compartment.09` | Ninth / turbine compartment | -46.5 .. -33.0 | aft turbine plant / aft main switchboard |
| `compartment.10` | Tenth / propulsion electric motor | -57.0 .. -46.5 | propulsion electric motor |

The first pressure compartment ends at approximately X=+66.5 m, before the reserved bow-sonar allocation. Torpedo-tube mouths may extend forward of that pressure-hull boundary without making the sonar/nose region part of `compartment.01`.

P-700 launch containers, the MGK-540 bow-array allocation, VVD bottles, sail equipment, shafting and steering gear are not pressure-hull compartments. The region approximately X=+66.5 .. +77 m remains the forward sonar/nose region, while X=-77 .. -57 m remains the external aft shaft/steering region rather than extending `compartment.10` to the propellers.

`Content/submarines/Antey/Antey.compartments.json` is the reviewable reference-derived source for this mapping. The corresponding records embedded in `Antey.authoring.json` carry stable semantic IDs, Russian display names, functional roles, role provenance, system tags and matching X ranges. `ProductionAnteyAssetDefinition` exposes that functional metadata while Simulation remains authoritative for damage, flooding, fire, atmosphere, crew, access boundaries and equipment state.
