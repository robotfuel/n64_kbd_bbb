i#include "SDL/SDL.h"

void HaxKBSetup();
int Init();
void QuitProgram();
int CaptureInput();

struct KB
{
	/* data */

};



//Globals
int boolCaptureIO = 0;
int boolRunProgram = 1;

SDL_Surface* screen = NULL;
SDL_Event event;
//TTF_Font *font = NULL;

int main( int argc, char* args[] )
{
	if(!Init())
	{
		return -1;
	}

	HaxKBSetup();

	CaptureInput();

	return 0;
}
void HaxKBSetup()
{
	printf("Press Start\n" );
	int waiting = 1;
	while(waiting)
	{
		while(SDL_PollEvent(&event))
		{
			if( event.type == SDL_KEYDOWN ) 
			{
				//Process key press
				waiting = 0;
				break;
			}
		}
	}
	printf("Press A\n" );
	printf("Press B\n" );
	printf("Press Up\n" );
	printf("Press Down\n");
}

int CaptureInput()
{
	boolCaptureIO = 1;

	while( boolCaptureIO )
	{
		//While there's an event to handle 
		while( SDL_PollEvent( &event ) ) 
		{
			//If a key was pressed 
			if( event.type == SDL_KEYDOWN ) 
			{
				//Process key press
			}
			else if( event.type == SDL_KEYUP)
			{
				//Process key up
			}
			else if( event.type == SDL_QUIT ) 
			{ 
				//Quit the program 
				QuitProgram(); 
			} 
			//If there's an event to handle 
		}
	}
}


int Init() 
{ 
	//Initialize all SDL subsystems 
	if( SDL_Init( SDL_INIT_EVERYTHING ) == -1 ) 
	{ 
		return 0; 
	} 
	//Set up the screen 
	screen = SDL_SetVideoMode( 640,480,32, SDL_SWSURFACE ); 
	//If there was an error in setting up the screen 
	if( screen == NULL ) 
	{ 
		return 0; 
	} 
	//Set the window caption 
	SDL_WM_SetCaption( "Projectx74 v2.0", NULL ); 
	//If everything initialized fine 
	return 1; 
}

void QuitProgram()
{
	boolCaptureIO = 0;
	boolRunProgram = 0;
	SDL_Quit();
}




