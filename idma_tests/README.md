## IDMA Tests

This folder contains basic tests for the iDMA IP. 
Currently, the following are supported:
- 1D transfers: both from L2 to L1 and from L1 to L2.
- 2D transfers: strides can be configured in the source and destination memory regions.
- 3D transfers: multiple 2D pages can be moved between L1 and L2.

To launch each test:
1. Move into the respective folder.
2. Launch the following:
    1. `make stimuli` : this will generate the randomized stimuli for the test. The randomized stimuli consist of transfer sizes, number of transfers to be executed, n-dimensional strides, etc ...
    2. `make all` : this will compile the C code.
    3. `make run` : this will launch the simulation in bash mode (use gui=1 for Modelsim gui)

### Ongoing

The following developements are ongoin:
- Tests for multi-core execution
- Tests for parallel transfer execution
- Tests for L1 to L1 data transfers.


