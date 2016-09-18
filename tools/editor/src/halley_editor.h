#pragma once

#include "prec.h"
#include "halley/file/filesystem.h"

namespace Halley
{
	class Project;
	class Preferences;

	class HalleyEditor final : public Game
	{
	public:
		HalleyEditor();
		~HalleyEditor();

		Project& loadProject(Path path);
		Project& createProject(Path path);

		bool hasProjectLoaded() const;
		Project& getProject() const;
		bool isHeadless() const { return headless; }

	protected:
		void init(const Environment& environment, const Vector<String>& args) override;
		int initPlugins(IPluginRegistry &registry) override;
		void initResourceLocator(String dataPath, ResourceLocator& locator) override;
		std::unique_ptr<Stage> startGame(HalleyAPI* api) override;

		String getName() const override;
		String getDataPath() const override;
		bool isDevBuild() const override;
		bool shouldCreateSeparateConsole() const override;

	private:
		std::unique_ptr<Project> project;
		std::unique_ptr<Preferences> preferences;
		Path sharedAssetsPath;
		bool headless = true;

		void parseArguments(const std::vector<String>& args);
	};
}