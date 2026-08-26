# OnStep mount control used by TouchFocus

TouchFocus uses the existing OnStepX LX200-compatible TCP command channel. It
does not define a new mount protocol. The implementation was independently
derived from the upstream `hjd1964/SmartHandController` and `hjd1964/OnStepX`
source trees.

Default connection:

- TCP port: `9999`
- OnStep station-mode address: `192.168.88.60`
- OnStep access-point address: `192.168.0.60`

The address is editable and persisted on the TouchFocus CONNECTION screen.

## Manual motion

| Direction | Press | Release / axis stop |
|---|---|---|
| North | `:Mn#` | `:Qn#` |
| South | `:Ms#` | `:Qs#` |
| East | `:Me#` | `:Qe#` |
| West | `:Mw#` | `:Qw#` |

The full emergency stop command is `:Q#`. Unlike an axis-specific release, it
also aborts an active GOTO.

## Manual slew rate

The TouchFocus screen follows the seven rates exposed by the original Smart
Hand Controller feature keys:

| Command | Rate |
|---|---:|
| `:R3#` | 2x |
| `:R4#` | 4x (default) |
| `:R5#` | 8x |
| `:R6#` | 20x |
| `:R7#` | 48x |
| `:R8#` | half of configured maximum GOTO rate |
| `:R9#` | configured maximum GOTO rate |

OnStepX accepts the complete `R0` through `R9` range; TouchFocus deliberately
uses `R3` through `R9` to match SmartHandController manual slew behavior.
