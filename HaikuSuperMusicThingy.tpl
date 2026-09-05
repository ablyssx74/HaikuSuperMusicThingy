name			$(NAME)
version			$(VERSION)-$(REVISION)
architecture	$(ARCH)
summary 		"SomaFM Music Player"
description 	"HaikuSuperMusicThingy is a free streaming media client for SomaFM. Fast, light, and fun!"
packager		"ablyss <supermusicthingy@epluribusunix.net>"
vendor			"Haiku Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(NAME) = $(VERSION)-$(REVISION)
	lib:libprojectM = 4.1.0-1
}
requires {
	haiku
	haiku_datatranslators
	nlohmann_json
	mpv$(is32bit)
	openal$(is32bit)
	curl$(is32bit)
	libsdl2$(is32bit)
	
}	
urls {
	"https://github.com/ablyssx74/HaikuSuperMusicThingy"
}
source-urls {
# Download
	"https://github.com/ablyssx74/HaikuSuperMusicThingy/archive/refs/tags/v1.0.0.tar.gz"
}
