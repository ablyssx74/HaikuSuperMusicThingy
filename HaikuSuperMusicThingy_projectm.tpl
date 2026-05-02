name			$(NAME)
version			$(VERSION)-1
architecture	$(ARCH)
summary 		"Portable streaming media client for SomaFM"
description 	"SuperMusicThingy is a free streaming media client for SomaFM. Fast, light, and fun!"
packager		"ablyss <jb@epluribusunix.net>"
vendor			"Haiku Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(NAME) = $(VERSION)-1
}
requires {
	haiku >= r1~beta5_hrev59183-1
	libglvnd >= 1.7.0-1
	nebula
	nlohmann_json
	mpv
	openal
	curl
	libsdl2
	
	
}	
urls {
	"https://github.com/ablyssx74/HaikuSuperMusicThingy"
}
source-urls {
# Download
	"https://github.com/ablyssx74/HaikuSuperMusicThingy/archive/refs/tags/v1.0.0.tar.gz"
}
