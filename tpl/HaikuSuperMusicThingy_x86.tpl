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
	haiku
	nlohmann_json
	mpv_x86
	openal_x86
	curl_x86
	
}	
urls {
	"https://github.com/ablyssx74/HaikuSuperMusicThingy"
}
source-urls {
# Download
	"https://github.com/ablyssx74/HaikuSuperMusicThingy/archive/refs/tags/v1.0.0.tar.gz"
}
