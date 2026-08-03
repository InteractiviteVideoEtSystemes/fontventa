/**
 * test_env.cpp — infrastructure commune de la suite gtest de libmedikit.
 *
 * libmedkit appelle Log()/Debug()/Error() (medkit/log.h). Sans callbacks
 * installés via SetLogFunctions(), ces appels déréférencent un pointeur nul :
 * on installe donc un Environment gtest GLOBAL (enregistré une seule fois, ici)
 * qui router les logs vers stdout/stderr. Les harnais historiques
 * (testsps.cpp, negotest.cpp) faisaient déjà ce câblage dans leur main().
 *
 * gtest_main fournit main() ; on enregistre l'Environment via un initialiseur
 * statique (AddGlobalTestEnvironment ne fait qu'empiler l'objet dans une liste,
 * ce qui est sûr avant l'appel à InitGoogleTest fait par gtest_main).
 */
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdarg>

#include <medkit/log.h>

namespace {

int DebugToStdout(const char *msg, va_list ap) { return vfprintf(stdout, msg, ap); }
int LogToStdout(const char *msg, va_list ap)   { return vfprintf(stdout, msg, ap); }
int ErrorToStderr(const char *msg, va_list ap) { return vfprintf(stderr, msg, ap); }

// Environment global : SetUp() une fois avant tous les tests.
class MedkitEnvironment : public ::testing::Environment
{
public:
	void SetUp() override
	{
		SetLogFunctions(DebugToStdout, LogToStdout, ErrorToStderr);
	}
};

// Enregistrement à l'initialisation statique (avant main de gtest_main).
::testing::Environment* const g_medkitEnv =
	::testing::AddGlobalTestEnvironment(new MedkitEnvironment);

} // namespace

// --- Smoke test : valide la chaîne compile/link/exécution gtest + libmedkit. --
TEST(Smoke, LogFunctionsInstalled)
{
	// Si on arrive ici, l'Environment a tourné : un Log() ne doit pas crasher.
	Log("[smoke] libmedikit gtest harness up\n");
	SUCCEED();
}
