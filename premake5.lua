workspace "ProjectContainer"
	configurations {"Debug", "Release"}
	platforms {"x32", "x64"}
	location "build"

filter "configurations:Debug"
	defines {"DEBUG"}

filter {"platforms:x32"} 
	architecture "x32"
filter {"platforms:x64"}
	architecture "x64"

filter "configurations:Release"
	defines {"NDEBUG"}
	optimize "On"

project "Executable"
	kind "ConsoleApp"
	language "C++"
	location "build"
	files {"**.cpp", "**.h"}