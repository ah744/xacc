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
#include "ScaffoldCompiler.hpp"
#include "GraphIR.hpp"
#include <regex>
#include "ScaffCCAPI.hpp"
#include "QasmToGraph.hpp"
#include "QuantumCircuit.hpp"

namespace xacc {

namespace quantum {

void ScaffoldCompiler::modifySource() {

	// Here we assume we've been given just
	// the body of the quantum code, as part
	// of an xacc __qpu__ kernel function.

	// First off, replace __qpu__ with 'module '
	kernelSource.erase(kernelSource.find("__qpu__"), 7);
	kernelSource = std::string("module ") + kernelSource;

	std::string qubitAllocationLine, cbitAllocationLine, cbitVarName, qbitVarName;
	std::map<int, int> cbitToQubit;

	if (typeToVarKernelArgs.find("qbit") != typeToVarKernelArgs.end()) {
		qubitAllocationLine = "qbit " + typeToVarKernelArgs["qbit"] + ";\n";
		qbitVarName = typeToVarKernelArgs["qbit"].substr(0, typeToVarKernelArgs["qbit"].find_first_of("["));
	} else {
		std::regex qbitName("qbit\\s.*");
		qubitAllocationLine = (*std::sregex_iterator(kernelSource.begin(),
				kernelSource.end(), qbitName)).str() + "\n";
		std::vector<std::string> splitQbit;
		boost::split(splitQbit, qubitAllocationLine, boost::is_any_of(" "));
		qbitVarName = splitQbit[1].substr(0, splitQbit[1].find_first_of("["));
	}

    // Create Cbit to Qbit mapping
	std::regex cbitName("cbit\\s.*");
	auto it = std::sregex_iterator(kernelSource.begin(), kernelSource.end(),
			cbitName);
	if (it != std::sregex_iterator()) {
		cbitAllocationLine = (*std::sregex_iterator(kernelSource.begin(),
				kernelSource.end(), cbitName)).str() + "\n";
		std::vector<std::string> splitCbit;
		boost::split(splitCbit, cbitAllocationLine, boost::is_any_of(" "));
		cbitVarName = splitCbit[1].substr(0,
				splitCbit[1].find_first_of("["));

		std::regex measurements(".*Meas.*");
		for (auto i = std::sregex_iterator(kernelSource.begin(),
				kernelSource.end(), measurements); i != std::sregex_iterator();
				++i) {
			auto measurement = (*i).str();
			boost::trim(measurement);

			boost::erase_all(measurement, "MeasZ");
			boost::erase_all(measurement, "(");
			boost::erase_all(measurement, ")");
			boost::erase_all(measurement, cbitVarName);
			boost::erase_all(measurement, qbitVarName);
			// Should now have [#] = [#]
			boost::erase_all(measurement, "[");
			boost::erase_all(measurement, "]");

			std::vector<std::string> splitVec;
			boost::split(splitVec, measurement, boost::is_any_of("="));
			auto cbit = splitVec[0];
			auto qbit = splitVec[1];
			boost::trim(cbit);
			boost::trim(qbit);

			cbitToQubit.insert(
					std::make_pair(std::stoi(cbit), std::stoi(qbit)));
		}
	}

	// conditional on measurements
	// FIXME FOR NOW WE ONLY ACCEPT format
	// 'if (creg[0] == 1) GATEOP'
	int counter = 1;
	std::vector<std::string> ifLines;
	std::regex ifstmts("if\\s?\\(\\w+\\[\\w+\\]\\s?=.*\\s?\\)\\s?");
	for (auto i = std::sregex_iterator(kernelSource.begin(), kernelSource.end(),
			ifstmts); i != std::sregex_iterator(); ++i) {
		std::vector<std::string> splitVec;
		std::string ifLine = (*i).str();
		boost::trim(ifLine);
		boost::split(splitVec, ifLine, boost::is_any_of(" "));
		conditionalCodeSegments.push_back("module foo" + std::to_string(counter) + "() {\n"
				+ qubitAllocationLine + "   " + splitVec[splitVec.size()-1] + ";\n}\nint main() {\n"
						"   foo" + std::to_string(counter) + "();\n}");
		counter++;

		ifLines.push_back(ifLine + ";\n");

        // Also get which cbit this conditional code belongs to
		int measurementGateId = -1;
		for (auto s : splitVec) {
			if (boost::contains(s, cbitVarName)) {
				boost::erase_all(s, "(");

				boost::erase_all(s, cbitVarName);
				boost::erase_all(s, "[");
				boost::erase_all(s, "]");

				conditionalCodeSegmentActingQubits.push_back(cbitToQubit[std::stoi(s)]);

				break;
			}
		}

	}

	// Erase the if lines from the main source
	// they are going to be represented with
	// conditional graphs.
	for (auto s : ifLines) {
		auto idx = kernelSource.find(s);
		kernelSource.erase(idx, s.size());
	}

	std::cout << "KERNELSRC: \n" << kernelSource << "\n";
	std::regex gates("\\s+*\\w+\\(.*");
	it = std::sregex_iterator(kernelSource.begin(), kernelSource.end(),
			gates);
	counter = 0;
	for (auto it = std::sregex_iterator(kernelSource.begin(), kernelSource.end(),
			gates); it != std::sregex_iterator(); ++it) {
		std::cout << "GateLine? " << (*it).str() << "\n";
		auto gateLine = (*it).str();
		auto firstParen = gateLine.find_first_of('(');
		auto secondParen = gateLine.find_first_of(')', firstParen);
		auto functionArguments = gateLine.substr(firstParen+1, (secondParen-firstParen)-1);
		auto args = gateLine.substr(firstParen+1, (secondParen-firstParen)-1);
		std::cout << "Args for this gate are " << args << "\n";
		std::vector<std::string> splitArgs;
		boost::split(splitArgs, args, boost::is_any_of(","));
		std::vector<std::string> params;
		for (auto a : splitArgs) {
			boost::trim(a);
			if (!boost::contains(a, qbitVarName)) {
				// This is a gate parameter... What do we do with it?
				params.push_back(a);
			}
		}

		gateIdToParameterMap.insert(std::make_pair(counter, params));

	}

	// Get the kernel name
	std::regex functionName("((\\w+)\\s*\\()\\s*");
	auto begin = std::sregex_iterator(kernelSource.begin(), kernelSource.end(),
			functionName);
	std::string fName = (*begin).str();

	std::string varAllocation = "", fargs;
	for (auto i : orderOfArgs) {
		auto key = i;
		auto value = typeToVarKernelArgs[i];
		if ("qbit" == key) {
			varAllocation += key + " " + value + ";\n   ";
			fargs += value.substr(0, value.find_first_of("[")) + ",";
		} else {

			varAllocation += key + " " + value + " = 0;\n   ";
			fargs += value + ",";
		}
	}

	if (!fargs.empty()) {
		fargs = fargs.substr(0, fargs.size()-1);
		boost::replace_first(fName, "(", "(" + fargs);
	}

	// Now wrap in a main function for ScaffCC
	kernelSource = kernelSource + std::string("\nint main() {\n   ") + varAllocation + fName
			+ std::string(");\n}");

	std::cout << "\n" << kernelSource << "\n";
}

std::shared_ptr<IR> ScaffoldCompiler::compile() {

	// Create an instance of our ScaffCC API
	// so that we can interact with a locally installed
	// scaffcc executable
	scaffold::ScaffCCAPI scaffcc;
	using ScaffoldGraphIR = GraphIR<QuantumCircuit>;

	// Compile the source code and return the QASM form
	// This will throw if it fails.
	auto qasm = scaffcc.getFlatQASMFromSource(kernelSource);

	std::cout << "QASM:\n" << qasm << "\n";

	// Get the Qasm as a Graph...
	auto circuitGraph = QasmToGraph::getCircuitGraph(qasm);

	int counter = 0;
	for (int i = 0; i < circuitGraph.order(); i++) {
		auto props = circuitGraph.getVertexProperties(i);
		auto gateName = std::get<0>(props);
		if (gateName != "InitialState" && gateName != "FinalState") {
			auto possibleParams = std::get<5>(props);
			if (!possibleParams.empty()) {
				// This is a parameterized gate
				for (int j = 0; j < possibleParams.size(); j++) {
					possibleParams[j] = gateIdToParameterMap[counter][j];
					std::cout << "Setting " << gateName << " param names to " << gateIdToParameterMap[counter][j] << "\n";
				}
			}
			counter++;
		}
	}


	// HERE we have main circuit graph, before conditional
	// if branches... So then for each conditional code statement,
	// get its circuit graph and add it to the main graph after
	// the addition of a COND conditional node that will enable the
	// conditional nodes if the measured cbit is a 1.

	// Get measurement acting qubits

	// So a COND node needs to know the gate id of the measurement gate
	// and the nodes to mark enabled if the measurement is a 1,
	if (!conditionalCodeSegments.empty()) {
		std::vector<QuantumCircuit> condGraphs;
		for (auto cond : conditionalCodeSegments) {
			auto condQasm = scaffcc.getFlatQASMFromSource(cond);
			auto g = QasmToGraph::getCircuitGraph(condQasm);
			condGraphs.push_back(g);
		}

		QasmToGraph::linkConditionalQasm(circuitGraph, condGraphs,
				conditionalCodeSegmentActingQubits);
	}


	// Create a GraphIR instance from that graph
	auto graphIR = std::make_shared<ScaffoldGraphIR>(circuitGraph);

	// Return the IR.
	return graphIR;
}

} // end namespace quantum

} // end namespace xacc

// Required in CPP file to be discovered by factory pattern
REGISTER_QCIOBJECT_WITH_QCITYPE(xacc::quantum::ScaffoldCompiler, "compiler",
		"scaffold");

