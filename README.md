	ABOUT
    
	this is a simple cli wav player written in C and using ALSA (linux only)
	it is currently inefficient in memory,
	because to read a .wav it is necessary to copy
	all the memory of the file into the program's
	memory before instead of reading the data on demand
	in this player you can play a list of.wav files
	within a directory (recursively if you enable this option)
	a file is identified as .wav only by its name, which means
	that the program does not perform a security check to ensure
	that a file with a .wav name is in fact a .wav
  
		--- OPERATION MODES ---	 
    
	Command mode:
	it's the mode you're in right now, where you set
	certain settings like playlistloop or volume (yes, the volume
	should be set here and not while a .wav is playing) and dictate
	specific commands for specific needs
	Player mode:
	this is the mode you find yourself in while a .wav
	s playing. in it there is some information about the current track
	a progress bar tha t updates at a constant rate and a list of
	commands (simpler to write) that you can write to get specific results


    --- HOW TO COMPILE ---
    
    $ make
    $ ./player [PATH] [RECURSIVE]

    if [PATH] (relative or global) is omitted, then the directory
    that will be used by the player will be the current directory ./
    [RECURSIVE] must be 1 if you want the program to read the
    directory recursively (default) or 0 otherwise

    run the program for more information
