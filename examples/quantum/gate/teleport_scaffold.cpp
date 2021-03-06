/***********************************************************************************
 * Copyright (c) 2016, UT-Battelle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the xacc nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contributors:
 *   Initial API and implementation - Alex McCaskey
 *
 **********************************************************************************/
#include "XACC.hpp"
#include "FireTensorAccelerator.hpp"

// Quantum Kernel executing teleportation of
// qubit state to another.
const std::string src("__qpu__ teleport (qbit qreg) {\n"
	"   cbit creg[2];\n"
	"   // Init qubit 0 to 1\n"
	"   X(qreg[0]);\n"
	"   // Now teleport...\n"
	"   H(qreg[1]);\n"
	"   CNOT(qreg[1],qreg[2]);\n"
	"   CNOT(qreg[0],qreg[1]);\n"
	"   H(qreg[0]);\n"
	"   creg[0] = MeasZ(qreg[0]);\n"
	"   creg[1] = MeasZ(qreg[1]);\n"
	"   if (creg[0] == 1) Z(qreg[2]);\n"
	"   if (creg[1] == 1) X(qreg[2]);\n"
	"}\n");

int main (int argc, char** argv) {

	// Create a convenient alias for our simulator...
	using CircuitSimulator = xacc::quantum::FireTensorAccelerator<6>;

	// Create a reference to the 6 qubit simulation Accelerator
	auto qpu = std::make_shared<CircuitSimulator>();

	// Allocate 3 qubits, give them a unique identifier...
	auto qubitReg = qpu->createBuffer("qreg", 3);
	using QubitRegisterType = decltype(qubitReg);

	// Construct a new XACC Program
	xacc::Program quantumProgram(qpu, src);

	// Build the program using Scaffold comipler
	// and output the Graph Intermediate Representation
	quantumProgram.build("--compiler scaffold "
			"--writeIR teleport.xir");

	// Retrieve the created kernel. It takes a 
	// qubit register as input
	auto teleport = quantumProgram.getKernel<QubitRegisterType>("teleport");

	// Execute the kernel with the qubit register!
	teleport(qubitReg);

	// Pretty print the resultant state
	qubitReg->printBufferState(std::cout);

	return 0;
}



