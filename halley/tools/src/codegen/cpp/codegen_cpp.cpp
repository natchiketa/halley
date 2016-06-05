#include <sstream>
#include "codegen_cpp.h"
#include "cpp_class_gen.h"
#include <set>
#include <halley/support/exception.h>

using namespace Halley;

static String toFileName(String className)
{
	std::stringstream ss;
	for (size_t i = 0; i < className.size(); i++) {
		if (className[i] >= 'A' && className[i] <= 'Z') {
			if (i > 0) {
				ss << '_';
			}
			ss << static_cast<char>(className[i] + 32);
		} else {
			ss << className[i];
		}
	}
	return ss.str();
}

static String upperFirst(String name)
{
	if (name[0] >= 'a' && name[0] <= 'z') {
		name[0] -= 32;
	}
	return name;
}

static String lowerFirst(String name)
{
	if (name[0] >= 'A' && name[0] <= 'Z') {
		name[0] += 32;
	}
	return name;
}

CodeGenResult CodegenCPP::generateComponent(ComponentSchema component)
{
	String className = component.name + "Component";

	CodeGenResult result;
	result.emplace_back(CodeGenFile("components/" + toFileName(className) + ".h", generateComponentHeader(component)));
	return result;
}

CodeGenResult CodegenCPP::generateSystem(SystemSchema system)
{
	String className = system.name + "System";

	CodeGenResult result;
	result.emplace_back(CodeGenFile("systems/" + toFileName(className) + ".h", generateSystemHeader(system)));
	return result;
}

CodeGenResult CodegenCPP::generateMessage(MessageSchema message)
{
	String className = message.name + "Message";

	CodeGenResult result;
	result.emplace_back(CodeGenFile("messages/" + toFileName(className) + ".h", generateMessageHeader(message)));
	return result;
}

CodeGenResult CodegenCPP::generateRegistry(const Vector<ComponentSchema>& components, const Vector<SystemSchema>& systems)
{
	Vector<String> registryCpp {
		"#include <halley.hpp>",
		"using namespace Halley;",
		"",
		"// System factory functions"
	};

	for (auto& sys: systems) {
		registryCpp.push_back("System* halleyCreate" + sys.name + "System();");
	}

	registryCpp.insert(registryCpp.end(), {
		"",
		"",
		"using SystemFactoryPtr = System* (*)();",
		"using SystemFactoryMap = HashMap<String, SystemFactoryPtr>;",
		"",
		"static SystemFactoryMap makeSystemFactories() {",
		"	SystemFactoryMap result;"
	});

	for (auto& sys : systems) {
		registryCpp.push_back("	result[\"" + sys.name + "System\"] = &halleyCreate" + sys.name + "System;");
	}

	registryCpp.insert(registryCpp.end(), { 
		"	return result;",
		"}",
		"",
		"namespace Halley {",
		"	std::unique_ptr<System> createSystem(String name) {",
		"		static SystemFactoryMap factories = makeSystemFactories();",
		"		return std::unique_ptr<System>(factories[name]());",
		"	}",
		"}"
	});

	Vector<String> registryH{
		"#pragma once",
		"",
		"namespace Halley {",
		"	std::unique_ptr<System> createSystem(String name);",
		"}"
	};

	CodeGenResult result;
	result.emplace_back(CodeGenFile("registry.cpp", registryCpp));
	result.emplace_back(CodeGenFile("registry.h", registryH));
	return result;
}

Vector<String> CodegenCPP::generateComponentHeader(ComponentSchema component)
{
	Vector<String> contents = {
		"#pragma once",
		"",
		"#include <halley.hpp>",
		""
	};

	CPPClassGenerator(component.name + "Component", "Halley::Component", CPPAccess::Public, true)
		.addAccessLevelSection(CPPAccess::Public)
		.addMember(VariableSchema(TypeSchema("int", false, true, true), "componentIndex", String::integerToString(component.id)))
		.addBlankLine()
		.addMembers(component.members)
		.addBlankLine()
		.addDefaultConstructor()
		.addBlankLine()
		.addConstructor(component.members)
		.finish()
		.writeTo(contents);

	return contents;
}

template <typename T, typename U>
Vector<U> convert(Vector<T> in, U(*f)(const T&))
{
	size_t sz = in.size();
	Vector<U> result;
	result.reserve(sz);
	for (size_t i = 0; i < sz; i++) {
		result.emplace_back(f(in[i]));
	}
	return result;
}

Vector<String> CodegenCPP::generateSystemHeader(SystemSchema system) const
{
	String methodName, methodArgType;
	bool methodConst;
	if (system.method == SystemMethod::Update) {
		methodName = "update";
		methodArgType = "Halley::Time";
		methodConst = false;
	} else if (system.method == SystemMethod::Render) {
		methodName = "render";
		methodArgType = "Halley::Painter&";
		methodConst = true;
	} else {
		throw Exception("Unsupported method in " + system.name + "System");
	}

	Vector<VariableSchema> familyArgs = { VariableSchema(TypeSchema(methodArgType), "p") };
	String stratImpl;
	if (system.strategy == SystemStrategy::Global) {
		stratImpl = "static_cast<T*>(this)->" + methodName + "(p);";
	} else if (system.strategy == SystemStrategy::Individual) {
		familyArgs.push_back(VariableSchema(TypeSchema("MainFamily&"), "entity"));
		stratImpl = "invokeIndividual(static_cast<T*>(this), &T::" + methodName + ", p, mainFamily);";
	} else if (system.strategy == SystemStrategy::Parallel) {
		familyArgs.push_back(VariableSchema(TypeSchema("MainFamily&"), "entity"));
		stratImpl = "invokeParallel(static_cast<T*>(this), &T::" + methodName + ", p, mainFamily);";
	} else {
		throw Exception("Unsupported strategy in " + system.name + "System");
	}

	Vector<String> contents = {
		"#pragma once",
		"",
		"#include <halley.hpp>",
		""
	};

	// Family headers
	std::set<String> included;
	for (auto& fam : system.families) {
		for (auto& comp : fam.components) {
			if (included.find(comp.name) == included.end()) {
				contents.emplace_back("#include \"../components/" + toFileName(comp.name + "Component") + ".h\"");
				included.emplace(comp.name);
			}
		}
	}
	for (auto& msg : system.messages) {
		contents.emplace_back("#include \"../messages/" + toFileName(msg.name + "Message") + ".h\"");
	}

	contents.insert(contents.end(), {
		"",
		"// Generated file; do not modify.",
		"template <typename T>"
	});

	auto sysClassGen = CPPClassGenerator(system.name + "SystemBase", "Halley::System", CPPAccess::Private)
		.addMethodDeclaration(MethodSchema(TypeSchema("Halley::System*"), {}, "halleyCreate" + system.name + "System", false, false, false, false, true))
		.addBlankLine()
		.addAccessLevelSection(CPPAccess::Protected);

	for (auto& fam : system.families) {
		sysClassGen
			.addClass(CPPClassGenerator(upperFirst(fam.name) + "Family")
				.addAccessLevelSection(CPPAccess::Public)
				.addMember(VariableSchema(TypeSchema("Halley::EntityId", true), "entityId"))
				.addBlankLine()
				.addMembers(convert<ComponentReferenceSchema, VariableSchema>(fam.components, [](auto& comp) { return VariableSchema(TypeSchema(comp.name + "Component* const"), lowerFirst(comp.name)); }))
				//.addMembers(from(fam.components) >> select([](auto& comp) { return VariableSchema(TypeSchema(comp.name + "Component* const"), lowerFirst(comp.name)); }) >> to_vector())
				.addBlankLine()
				.addTypeDefinition("Type", "Halley::FamilyType<" + String::concatList(convert<ComponentReferenceSchema, String>(fam.components, [](auto& comp) { return comp.name + "Component"; }), ", ") + ">")
				.finish())
			.addBlankLine();
	}

	if ((int(system.access) & int(SystemAccess::API)) != 0) {
		sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("Halley::HalleyAPI&"), {}, "getAPI", true), "return doGetAPI();");
	}
	if ((int(system.access) & int(SystemAccess::World)) != 0) {
		sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("Halley::World&"), {}, "getWorld", true), "return doGetWorld();");
	}
	bool hasReceive = false;
	Vector<String> msgsReceived;
	for (auto& msg : system.messages) {
		if (msg.send) {
			sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema("Halley::EntityId"), "entityId"), VariableSchema(TypeSchema(msg.name + "Message&", true), "msg") }, "sendMessage"), "sendMessageGeneric(entityId, msg);");
		}
		if (msg.receive) {
			hasReceive = true;
			msgsReceived.push_back(msg.name + "Message::messageIndex");
		}
	}

	sysClassGen
		.addBlankLine()
		.addAccessLevelSection(system.strategy == SystemStrategy::Global ? CPPAccess::Protected : CPPAccess::Private)
		.addMembers(convert<FamilySchema, VariableSchema>(system.families, [](auto& fam) { return VariableSchema(TypeSchema("Halley::FamilyBinding<" + upperFirst(fam.name) + "Family>"), fam.name + "Family"); }))
		.addBlankLine()
		.addAccessLevelSection(CPPAccess::Private)
		.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema(methodArgType), "p") }, methodName + "Base", false, false, true, true), stratImpl)
		.addBlankLine();

	if (hasReceive) {
		Vector<String> body = { "switch (msgIndex) {" };
		for (auto& msg : system.messages) {
			if (msg.receive) {
				body.emplace_back("case " + msg.name + "Message::messageIndex: onMessagesReceived(reinterpret_cast<" + msg.name + "Message**>(msgs), idx, n); break;");
			}
		}
		body.emplace_back("}");
		sysClassGen
			.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema("int"), "msgIndex"), VariableSchema(TypeSchema("Halley::Message**"), "msgs"), VariableSchema(TypeSchema("size_t*"), "idx"), VariableSchema(TypeSchema("size_t"), "n") }, "onMessagesReceived", false, false, true, true), body)
			.addBlankLine()
			.addLine("template <typename M>")
			.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema("M**"), "msgs"), VariableSchema(TypeSchema("size_t*"), "idx"), VariableSchema(TypeSchema("size_t"), "n") }, "onMessagesReceived"), "for (size_t i = 0; i < n; i++) static_cast<T*>(this)->onMessageReceived(*msgs[i], mainFamily[idx[i]]);")
			.addBlankLine();
	}

	sysClassGen
		.addAccessLevelSection(CPPAccess::Public)
		.addCustomConstructor({}, { VariableSchema(TypeSchema(""), "System", "{" + String::concatList(convert<FamilySchema, String>(system.families, [](auto& fam) { return "&" + fam.name + "Family"; }), ", ") + "}, {" + String::concatList(msgsReceived, ", ") + "}") })
		.finish()
		.writeTo(contents);

	contents.push_back("");

	contents.push_back("/*");
	contents.push_back("// Implement this:");
	auto actualSys = CPPClassGenerator(system.name + "System", system.name + "SystemBase<" + system.name + "System>", CPPAccess::Public, true)
		.addAccessLevelSection(CPPAccess::Public)
		.addMethodDeclaration(MethodSchema(TypeSchema("void"), familyArgs, methodName, methodConst));

	for (auto& msg : system.messages) {
		if (msg.receive) {
			actualSys.addMethodDeclaration(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema(msg.name + "Message&", true), "msg"), VariableSchema(TypeSchema("MainFamily&"), "entity") }, "onMessageReceived"));
		}
	}

	actualSys
		.finish()
		.writeTo(contents);

	contents.push_back("*/");

	return contents;
}

Vector<String> CodegenCPP::generateMessageHeader(MessageSchema message)
{
	Vector<String> contents = {
		"#pragma once",
		"",
		"#include <halley.hpp>",
		""
	};

	CPPClassGenerator(message.name + "Message", "Halley::Message", CPPAccess::Public, true)
		.addAccessLevelSection(CPPAccess::Public)
		.addMember(VariableSchema(TypeSchema("int", false, true, true), "messageIndex", String::integerToString(message.id)))
		.addBlankLine()
		.addMembers(message.members)
		.addBlankLine()
		.addDefaultConstructor()
		.addBlankLine()
		.addConstructor(message.members)
		.addBlankLine()
		.addMethodDefinition(MethodSchema(TypeSchema("size_t"), {}, "getSize", true, false, true, true), "return sizeof(" + message.name + "Message);")
		.finish()
		.writeTo(contents);

	return contents;
}