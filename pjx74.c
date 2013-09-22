// Standard header files
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

// Driver header file
#include "prussdrv.h"
#include <pruss_intc_mapping.h>
#include "SDL/SDL.h"

/******************************************************************************
* Local Macro Declarations                          *
******************************************************************************/

#define PRU_NUM   0
#define ADDEND1   0x98765400u
#define ADDEND2   0x12345678u
#define ADDEND3   0x10210210u

#define DDR_BASEADDR     0x80000000
#define OFFSET_DDR     0x00001000
#define OFFSET_SHAREDRAM 2048      //equivalent with 0x00002000

#define PRUSS0_SHARED_DATARAM   4

#define MAX_JOY_RANGE 32767
#define MIN_JOY_RANGE 32768
#define POSITIVE_AXIS 0
#define NEGATIVE_AXIS 1

/******************************************************************************
* Local Method Definitions                            *
******************************************************************************/
void HaxKBSetup();
int InitSDL();
int InitPRU();
void QuitProgram();
int CaptureInput();
static int LOCAL_exampleInit ( );
static unsigned short LOCAL_examplePassed ( unsigned short pruNum );


/******************************************************************************
* Global variables                              *
******************************************************************************/
int boolCaptureIO = 0;
int boolRunProgram = 1;

SDL_Surface* screen = NULL;
SDL_Event event;
//TTF_Font *font = NULL;

static int mem_fd;
static void *ddrMem, *sharedMem;

static unsigned int *sharedMem_int;


/******************************************************************************
* N64 Controller buttons and shit structs                            *
******************************************************************************/

typedef struct 
{
    int A;
    int B;
    int Z;
    int S;
    int Du;
    int Dd;
    int Dl;
    int Dr;
    //joy reset 8
    //null 9
    int L;
    int R;
    int Cu;
    int Cd;
    int Cl;
    int Cr;
} N64_Button;

N64_Button N64BUTTONS;


typedef struct 
{
    int Ju;
    int Jd;
    int Jl;
    int Jr;
} N64_Joydir;
N64_Joydir N64JOYDIRECTIONS;



/******************************************************************************
* Structs for mapping SDL to N64                                 *
******************************************************************************/

//Map for a key to a button
typedef struct n64k2bm
{
    int MyN64but;
    SDLKey MyKey;

    int MyID;
    struct n64k2bm* MyNext;

} N64Key2ButMap;

//map for key to direction
typedef struct n64k2jm
{
    int MyN64dir;
    SDLKey MyKey;

    int MyID;
    struct n64k2jm* MyNext;

} N64Key2DirMap;

//map for joystick button to button
typedef struct n64jb2bm
{
    int MyN64but;
    //mapped from:
    int MyJoystick;
    int MyJoybutton;

    int MyID;
    struct n64jb2bm* MyNext;

} Joybut2N64ButMap;

//map for joystick button to directopm
typedef struct n64jb2dm
{
    int MyN64dir;

    //Mapped from
    int MyJoystick;
    int MyJoybutton;
    
    int MyID;
    struct n64jb2dm* MyNext;

} Joybut2N64DirMap;


//map for joystick diretion to button
typedef struct n64jd2bm
{
    int MyN64but;

    //Mapped from
    int MyJoystick; 
    int MyJoyaxis; // X,Y,Z,Etc
    int MyJoydir; //0 for +, 1 for -

    int MyID;
    struct n64jd2bm* MyNext;

} Joydir2N64ButMap;

//map for joystick diretion to direction
typedef struct n64jd2dm
{
    int MyN64dir;

    //Mapped from
    int MyJoystick; 
    int MyJoyaxis; // X,Y,Z,Etc
    int MyJoydir; //0 for +, 1 for -

    int MyID;
    struct n64jd2dm* MyNext;

} Joydir2N64DirMap;




/******************************************************************************
* My Controller Representation                              *
******************************************************************************/
N64Key2ButMap* Head_K2BMap = NULL;
N64Key2DirMap* Head_K2DMap = NULL;

Joybut2N64ButMap* Head_JB2BMap = NULL;
Joybut2N64DirMap* Head_JB2DMap = NULL;

Joydir2N64ButMap * Head_JD2BMap = NULL;
Joydir2N64DirMap * Head_JD2DMap = NULL;

float MyJoyU = 0;
float MyJoyD = 0;
float MyJoyL = 0;
float MyJoyR = 0;

float MyDeadzone;      // [0,1) default 0
float MyRangePrescale;  // (0,1] default 1
float MyRange;    // (Deadzone, 1] default 1



/******************************************************************************
*  Managing SDL to N64 mappings                                   *
******************************************************************************/
//Keyboard Key to N64 Button
void AddKey2ButMap(int key, int but)
{
    N64Key2ButMap* tmp = (N64Key2ButMap*)malloc(sizeof(N64Key2ButMap));
    tmp->MyKey = key;
    tmp->MyN64but = but;
    tmp->MyNext = NULL;
    //Beginning
    if(Head_K2BMap == NULL)
    {
        Head_K2BMap = tmp;
        Head_K2BMap->MyID = 0;
    }
    else
    {
        N64Key2ButMap* currentmap = Head_K2BMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveKey2ButMap(int mapID)
{
    N64Key2ButMap* currentmap = Head_K2BMap;

    if(currentmap->MyID == mapID)
    {
        Head_K2BMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing key to button map\n");
            
            N64Key2ButMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}

//Keyboard Key to N64 Direction
void AddKey2DirMap(int key, int dir)
{
    N64Key2DirMap* tmp = (N64Key2DirMap*)malloc(sizeof(N64Key2DirMap));
    tmp->MyKey = key;
    tmp->MyN64dir = dir;
    tmp->MyNext = NULL;
    //Beginning
    if(Head_K2DMap == NULL)
    {
        Head_K2DMap = tmp;
        Head_K2DMap->MyID = 0;
    }
    else
    {
        N64Key2DirMap* currentmap = Head_K2DMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveKey2DirMap(int mapID)
{
    N64Key2DirMap* currentmap = Head_K2DMap;

    if(currentmap->MyID == mapID)
    {
        Head_K2DMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing key to N64 joystick map\n");
            
            N64Key2DirMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}

//Joystick button to N64 Button
void AddJoybut2N64ButMap(int joy, int jbut, int nbut)
{
    Joybut2N64ButMap* tmp = (Joybut2N64ButMap*)malloc(sizeof(Joybut2N64ButMap));
    tmp->MyJoystick = joy;
    tmp->MyJoybutton = jbut;
    tmp->MyN64but = nbut;
    tmp->MyNext = NULL;

    //Beginning
    if(Head_JB2BMap == NULL)
    {
        Head_JB2BMap = tmp;
        Head_JB2BMap->MyID = 0;
    }
    else
    {
        Joybut2N64ButMap* currentmap = Head_JB2BMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveJoybut2N64ButMap(int mapID) 
{
    Joybut2N64ButMap* currentmap = Head_JB2BMap;

    if(currentmap->MyID == mapID)
    {
        Head_JB2BMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing Joystick Button to N64 button map\n");
            
            Joybut2N64ButMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}

//Joystick button to N64 Direction
void AddJoybut2N64DirMap(int joy, int jbut, int dir)
{
    Joybut2N64DirMap* tmp = (Joybut2N64DirMap*)malloc(sizeof(Joybut2N64DirMap));
    tmp->MyJoystick = joy;
    tmp->MyJoybutton = jbut;
    tmp->MyN64dir = dir;
    tmp->MyNext = NULL;

    //Beginning
    if(Head_JB2DMap == NULL)
    {
        Head_JB2DMap = tmp;
        Head_JB2DMap->MyID = 0;
    }
    else
    {
        Joybut2N64DirMap* currentmap = Head_JB2DMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveJoybut2N64DirMap(int mapID)
{
    Joybut2N64DirMap* currentmap = Head_JB2DMap;

    if(currentmap->MyID == mapID)
    {
        Head_JB2DMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing Joystick Dirton to N64 button map\n");
            
            Joybut2N64DirMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}

//Joystick direction to N64 Bitton
void AddJoydir2N64ButMap(int joy, int axis, int jdir, int but)
{
    Joydir2N64ButMap* tmp = (Joydir2N64ButMap*)malloc(sizeof(Joydir2N64ButMap));
    tmp->MyJoystick = joy;
    tmp->MyJoyaxis = axis;
    tmp->MyJoydir = jdir;

    tmp->MyN64but = but;
    tmp->MyNext = NULL;

    //Beginning
    if(Head_JD2BMap == NULL)
    {
        Head_JD2BMap = tmp;
        Head_JD2BMap->MyID = 0;
    }
    else
    {
        Joydir2N64ButMap* currentmap = Head_JD2BMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveJoydir2N64ButMap(int mapID)
{
    Joydir2N64ButMap* currentmap = Head_JD2BMap;

    if(currentmap->MyID == mapID)
    {
        Head_JD2BMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing Joystick Button to N64 button map\n");
            
            Joydir2N64ButMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}

//Joystick direction to N64 Direction
void AddJoydir2N64DirMap(int joy, int axis, int jdir, int ndir)
{
    Joydir2N64DirMap* tmp = (Joydir2N64DirMap*)malloc(sizeof(Joydir2N64DirMap));
    tmp->MyJoystick = joy;
    tmp->MyJoyaxis = axis;
    tmp->MyJoydir = jdir;

    tmp->MyN64dir = ndir;
    tmp->MyNext = NULL;

    //Beginning
    if(Head_JD2DMap == NULL)
    {
        Head_JD2DMap = tmp;
        Head_JD2DMap->MyID = 0;
    }
    else
    {
        Joydir2N64DirMap* currentmap = Head_JD2DMap;

        while(currentmap->MyNext != NULL)
        {
            currentmap = currentmap->MyNext;
        }

        tmp->MyID = currentmap->MyID +1;
        currentmap->MyNext = tmp;
    }
}
void RemoveJoydir2N64DirMap(int mapID)
{
    Joydir2N64DirMap* currentmap = Head_JD2DMap;

    if(currentmap->MyID == mapID)
    {
        Head_JD2DMap = currentmap->MyNext;
        free(currentmap);
        return;
    }

    while(currentmap->MyNext != NULL)
    {
        if(currentmap->MyNext->MyID == mapID)
        {
            printf("Removing Joystick Dirton to N64 button map\n");
            
            Joydir2N64DirMap* tmp = currentmap->MyNext;
            currentmap->MyNext = currentmap->MyNext->MyNext;
            free(tmp);

            return;
        }
    }
}



/******************************************************************************
* Setting the PRU Shared Memory N64 Button states                               *
******************************************************************************/
void SetPRU_ButState(int button, int l_or_0)
{
    if(l_or_0)
    {
        //
        //sharedMem_int[OFFSET_SHAREDRAM+0] = 1;          //0, 1,  2, 3  flag to keep running
        sharedMem_int[OFFSET_SHAREDRAM+1] = 
            sharedMem_int[OFFSET_SHAREDRAM+1] | (1<<(31-button));    //4, 5,  6, 7  state

        //sharedMem_int[OFFSET_SHAREDRAM+2] = 0x050002;       //8, 9, 10,11  status
        //sharedMem_int[OFFSET_SHAREDRAM+3] = 0;          //12,13,14,15  did pru exit
        //sharedMem_int[OFFSET_SHAREDRAM+4] = 0;          //16,17,18,19  Read value
    }
    else
    {
        sharedMem_int[OFFSET_SHAREDRAM+1] = 
            sharedMem_int[OFFSET_SHAREDRAM+1] & (~(1<<(31-button)));
    }
}

void SetPRU_DirState(int direction, int pressed)
{
    if(direction == N64JOYDIRECTIONS.Ju)
    {
        if(pressed)
        {
            MyJoyU = 1;
        }
        else
        {
            MyJoyU = 0;
        }
    }
    else if(direction == N64JOYDIRECTIONS.Jd)
    {
        if(pressed)
        {
            MyJoyD = 1;
        }
        else
        {
            MyJoyD = 0;
        }
    }
    else if(direction == N64JOYDIRECTIONS.Jl)
    {
        if(pressed)
        {
            MyJoyL = 1;
        }
        else
        {
            MyJoyL = 0;
        }
    }
    else if(direction == N64JOYDIRECTIONS.Jr)
    {
        if(pressed)
        {
            MyJoyR = 1;
        }
        else
        {
            MyJoyR = 0;
        }
    }
}



/******************************************************************************
* Handling Key Events                                  *
******************************************************************************/
void HandleKeyEvent(SDLKey key, int l_or_0) //1 for pressed, 0 for released
{
    N64Key2ButMap* currentmap1 = Head_K2BMap;
    while(currentmap1 != NULL)
    {
        if(currentmap1->MyKey == key)
        {
            SetPRU_ButState(currentmap1->MyN64but,l_or_0);
        }
        currentmap1 = currentmap1->MyNext;
    }


    N64Key2DirMap* currentmap2 = Head_K2DMap;
    while(currentmap2 != NULL)
    {
        if(currentmap2->MyKey == key)
        {
            //1 for pressed, 0 for release
            SetPRU_DirState(currentmap2->MyN64dir,l_or_0);
        }
        currentmap2 = currentmap2->MyNext;
    }
}


/******************************************************************************
* Handling Joy Button Events                              *
******************************************************************************/
void HandleJoybutEvent(int joy, int button, int l_or_0) //1 for pressed, 0 for released
{
    Joybut2N64ButMap* currentmap1 = Head_JB2BMap;
    while(currentmap1 != NULL)
    {
        if(currentmap1->MyJoystick == joy)
        {
            if(currentmap1->MyJoybutton == button)
            {
                SetPRU_ButState(currentmap1->MyN64but,l_or_0);
            }
        }
        currentmap1 = currentmap1->MyNext;
    }


    Joybut2N64DirMap* currentmap2 = Head_JB2DMap;
    while(currentmap2 != NULL)
    {
        if(currentmap2->MyJoystick == joy)
        {
            if(currentmap2->MyJoybutton == button)
            {
                SetPRU_DirState(currentmap2->MyN64dir,l_or_0);
            }
        }
        currentmap2 = currentmap2->MyNext;
    }
}

//Joy direction handled in input check loop





int InputMethod = -1;
/******************************************************************************
*
*                                   MAIN            
*                                                    
*                             
******************************************************************************/
int main( int argc, char* args[] )
{
    printf("Starting Program\n");

    N64BUTTONS.A = 0;
    N64BUTTONS.B = 1;
    N64BUTTONS.Z = 2;
    N64BUTTONS.S = 3;
    N64BUTTONS.Du = 4;
    N64BUTTONS.Dd = 5;
    N64BUTTONS.Dl = 6;
    N64BUTTONS.Dr = 7;
    N64BUTTONS.L = 10;
    N64BUTTONS.R = 11;
    N64BUTTONS.Cu = 12;
    N64BUTTONS.Cd = 13;
    N64BUTTONS.Cl = 14;
    N64BUTTONS.Cr = 15;

    N64JOYDIRECTIONS.Ju = 0;
    N64JOYDIRECTIONS.Jd = 1;
    N64JOYDIRECTIONS.Jl = 2;
    N64JOYDIRECTIONS.Jr = 3;

    while(1)
    {
        char text[20];
        fflush(stdout); /* http://c-faq.com/stdio/fflush.html */

        printf("Are you using keyboard or joystick? (enter k/s)\n");
        fgets(text, sizeof text, stdin);
        if(text[0] == 'k')
        {
            InputMethod = 0;
            break;
        }
        else if(text[0] == 's')
        {
            InputMethod = 1;
            break;
        }
        printf("Uh just k or s pls you tard\n");
    }

    printf("Initializing SDL\n");
    if(!InitSDL())
    {
        printf("Couldn't initialize SDL\n");
        return -1;
    }


    printf("Initializing PRU\n");
    //Start Pru Device
    if(InitPRU())
    {
        printf("Couldn't initialize PDU\n");
        return -2; 
    }

    
    


    //Use this for now to setup mapping
    printf("Starting HAX Setup, ");
    if(InputMethod == 0)
    {
        printf("Keyboard Input\n");
    }
    else
    {
        printf("Joystick Input\n");
    }
    HaxKBSetup();



    printf("Starting input capture\n");
    CaptureInput();

    return 0;
}





/******************************************************************************
* Initalizing SDL Library                              *
******************************************************************************/
SDL_Joystick** MyJoysticks = NULL;
int MyNumJoysticks;
int InitSDL()
{
    //Initialize all SDL subsystems
    if( SDL_Init( SDL_INIT_EVERYTHING ) == -1 )
    {
        printf("Couldn't initialize everything\n");
        return 0;
    }
    //Set up the screen
    screen = SDL_SetVideoMode( 640,480,32, SDL_SWSURFACE );
    //If there was an error in setting up the screen
    if( screen == NULL )
    {
        printf("Couldn't initialize screen\n");
        return 0;
    }
    //Set the window caption
    SDL_WM_SetCaption( "Projectx74 v2.0", NULL );


    if(InputMethod == 1)
    {
        MyNumJoysticks = SDL_NumJoysticks();
        printf("%i joysticks were found.\n\n", MyNumJoysticks );
        printf("The names of the joysticks are:\n");
        
        MyJoysticks = malloc(sizeof(SDL_Joystick*)*MyNumJoysticks);
        SDL_JoystickEventState(SDL_ENABLE);
        int i;        
        for( i=0; i < MyNumJoysticks; i++ ) 
        {
            MyJoysticks[i] = SDL_JoystickOpen(i);
            printf("%s\n", SDL_JoystickName(i));
        }
    }

    //If everything initialized fine
    return 1;
}


/******************************************************************************
* Initalizing PRU binary and intterupts                          *
******************************************************************************/
int InitPRU()
{
    unsigned int ret;
    tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;

    /* Initialize the PRU */
    prussdrv_init ();

    /* Open PRU Interrupt */
    ret = prussdrv_open(PRU_EVTOUT_0);
    if (ret)
    {
        printf("prussdrv_open open failed\n");
        return (ret);
    }

    /* Get the interrupt initialized */
    prussdrv_pruintc_init(&pruss_intc_initdata);

    /* Initialize example */
    //printf("\tINFO: Initializing example.\r\n");
    LOCAL_exampleInit(PRU_NUM);


    /* Allocate Shared PRU memory. */
    printf("Allocating Shared PRU Memory\r\n");
    prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, &sharedMem);
    sharedMem_int = (unsigned int*) sharedMem;

    sharedMem_int[OFFSET_SHAREDRAM+0] = 1;        //0, 1,  2, 3  flag to keep running
    sharedMem_int[OFFSET_SHAREDRAM+1] = 0x00000000;  //4, 5,  6, 7  state (0b1101)
    sharedMem_int[OFFSET_SHAREDRAM+2] = 0x050002;         //8, 9, 10,11  status
    sharedMem_int[OFFSET_SHAREDRAM+3] = 0;        //12,13,14,15  did pru exit
    sharedMem_int[OFFSET_SHAREDRAM+4] = 0;        //16,17,18,19  Read value


    /* Execute example on PRU */
    printf("Executing PRU.\n");
    prussdrv_exec_program (PRU_NUM, "./pjx74.bin");

    return 0;
}


/*****************************************************************************
* Input Setup                      *
*****************************************************************************/
void GrabButtonMap(int but)
{
    int waiting = 1;
    while(waiting)
    {
        while(SDL_PollEvent(&event))
        {
            if(InputMethod == 0)//keyboard
            {
                if( event.type == SDL_KEYDOWN )
                {
                    //Process key press
                    AddKey2ButMap(event.key.keysym.sym, but);
                    waiting = 0;
                    break;
                }
                
            }
            else //joystick
            {
                if (event.type == SDL_JOYBUTTONDOWN)
                {
                    AddJoybut2N64ButMap(event.jbutton.which,
                        event.jbutton.button,but);
                    waiting = 0;
                    break;
                }
                else if (event.type == SDL_JOYAXISMOTION)
                {
                    //Max = 32767
                    if(event.jaxis.value > 16383)
                    {
                        AddJoydir2N64ButMap(
                            event.jaxis.which,
                            event.jaxis.axis,
                            POSITIVE_AXIS,
                            but);

                        waiting = 0;
                        break;
                    }
                    //min = -32767
                    else if (event.jaxis.value < -16384)
                    {
                        AddJoydir2N64ButMap(
                            event.jaxis.which,
                            event.jaxis.axis,
                            NEGATIVE_AXIS,
                            but);
                        
                        waiting = 0;
                        break;
                    }
                }
            }

            if( event.type == SDL_QUIT )
            {
                //Quit the program
                printf("Got Quit event\n");
                QuitProgram();
            }

        }
    }
}
void GrabDirMap(int dir)
{
    int waiting = 1;
    while(waiting)
    {
        while(SDL_PollEvent(&event))
        {
            if(InputMethod == 0)
            {
                if( event.type == SDL_KEYDOWN )
                {
                    //Process key press
                    AddKey2DirMap(event.key.keysym.sym, dir);
                    waiting = 0;
                    break;
                }
            }
            else
            {
                if (event.type == SDL_JOYBUTTONDOWN)
                {
                    AddJoybut2N64DirMap(event.jbutton.which,
                        event.jbutton.button,dir);
                    waiting = 0;
                    break;
                }
                else if (event.type == SDL_JOYAXISMOTION)
                {
                    //Max = 32767
                    if(event.jaxis.value > 16383)
                    {
                        AddJoydir2N64DirMap(
                            event.jaxis.which,
                            event.jaxis.axis,
                            POSITIVE_AXIS,
                            dir);

                        waiting = 0;
                        break;
                    }
                    //min = -32767
                    else if (event.jaxis.value < -16384)
                    {
                        AddJoydir2N64DirMap(
                            event.jaxis.which,
                            event.jaxis.axis,
                            NEGATIVE_AXIS,
                            dir);
                        
                        waiting = 0;
                        break;
                    }
                }
            }
            if( event.type == SDL_QUIT )
            {
                //Quit the program
                printf("Got Quit event\n");
                QuitProgram();
            }
        }
    }
}

void ClearEvents()
{
    SDL_Delay(500);
    printf("Clearing events\n");
    //Clear the events I guess
    while(SDL_PollEvent(&event))
    {
        if( event.type == SDL_QUIT )
        {
            //Quit the program
            printf("Got Quit event\n");
            QuitProgram();
        }
    }
    SDL_Delay(500);
}

void HaxKBSetup()
{
    printf("\n\nNOTE: If you want to assign from a joystick, ");
    printf("you must put the stick back to neutral before assignment ASAP.\n\n");


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for A\n" );
    GrabButtonMap(N64BUTTONS.A);
    

    if(InputMethod){ ClearEvents();}
    printf("Press whatever for B\n" );
    GrabButtonMap(N64BUTTONS.B);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Z\n" );
    GrabButtonMap(N64BUTTONS.Z);
    

    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Start\n" );
    GrabButtonMap(N64BUTTONS.S);

    if(InputMethod){ ClearEvents();}
    printf("Press whatever for L\n" );
    GrabButtonMap(N64BUTTONS.L);

    if(InputMethod){ ClearEvents();}
    printf("Press whatever for R\n");
    GrabButtonMap(N64BUTTONS.R);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for C-up\n");
    GrabButtonMap(N64BUTTONS.Cu);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for C-down\n");
    GrabButtonMap(N64BUTTONS.Cd);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for C-left\n");
    GrabButtonMap(N64BUTTONS.Cl);

 
    if(InputMethod){ ClearEvents();}
    printf("Press whatever for C-right\n");
    GrabButtonMap(N64BUTTONS.Cr);
    

    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Joy Up\n");
    GrabDirMap(N64JOYDIRECTIONS.Ju);
    
  
    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Joy Down\n");
    GrabDirMap(N64JOYDIRECTIONS.Jd);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Joy Left\n");
    GrabDirMap(N64JOYDIRECTIONS.Jl);


    if(InputMethod){ ClearEvents();}
    printf("Press whatever for Joy Right\n");
    GrabDirMap(N64JOYDIRECTIONS.Jr);

}

/*****************************************************************************
* Shit that actually captures input from SDL                    *
*****************************************************************************/
int CaptureInput()
{
    boolCaptureIO = 1;
    MyJoyR = 0;
    MyJoyL = 0;
    MyJoyU = 0;
    MyJoyD = 0;
    float x = 0;
    float y = 0;
    float th = 0;
    float len = 0;
    int x2bc = 0;
    int y2bc = 0;
    int numAx = 0;
    
    Joydir2N64DirMap* currentmap0 = NULL;
    Joydir2N64ButMap* currentmap1 = NULL;

    // tan(a) = y/x 
    // len = Min( sqrt(x*x+y*y), 1)
    // 



    while( boolCaptureIO )
    {
        int numMaps = 0;
        //--------------------------
        //Update Joystick state
        //--------------------------
        //MyJoyX is set by keys or buttons.
        
        x = MyJoyR - MyJoyL;
        y = MyJoyU - MyJoyD;

        //traverse the joy to direction mappings
        if(InputMethod)
        {
            
            currentmap0 = Head_JD2DMap;
            while (currentmap0 != NULL)
            {
                //This should hopefully always be true:
                if(currentmap0->MyJoystick < MyNumJoysticks)
                {
                    short val = SDL_JoystickGetAxis(
                            MyJoysticks[currentmap0->MyJoystick],
                            currentmap0->MyJoyaxis
                            );

                    if(currentmap0->MyJoydir == POSITIVE_AXIS)
                    {
                        if(val > 0)
                        {
                            if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Ju)
                            {
                                y += ((float)val / 32767.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jd)
                            {
                                //wtf
                                y -= ((float)val / 32767.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jl)
                            {
                                x -= ((float)val / 32767.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jr)
                            {
                                //wtf
                                numMaps++;
                                x += ((float)val / 32767.0);
                            }
                        }
                    }
                    else
                    {
                        if(val < 0)
                        {
                            if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Ju)
                            {
                                y -= ((float)val / 32768.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jd)
                            {
                                y += ((float)val / 32768.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jl)
                            {
                                x += ((float)val / 32768.0);
                            }
                            else if( currentmap0->MyN64dir == N64JOYDIRECTIONS.Jr)
                            {   
                                x -= ((float)val / 32768.0);
                            }
                        }
                    }
                }

                currentmap0 = currentmap0->MyNext;
            }
        }

        if(!(x == 0 && y == 0))
        {
            len = sqrt(x*x+y*y);
            if(len > 1)
            {
                len = 1;
            }
            th = atan2(y,x);

            x = len*cos(th);
            y = len*sin(th); 
        }
        if(x >= 0)
        {
            x2bc = (unsigned int)((x*127) +0.5);
        }
        else
        {
            x2bc = (unsigned int)(255.5+(x*127));
        }
        if(y >= 0)
        {
            y2bc = (unsigned int)(y*127 +0.5);
        }
        else
        {
            y2bc = (unsigned int)(255.5+(y*127) +0.5);
        }
        unsigned int temp = sharedMem_int[OFFSET_SHAREDRAM+1];

        temp = temp & 0xFFFF0000;
        temp = temp | (y2bc);
        temp = temp | (x2bc<<8);

        sharedMem_int[OFFSET_SHAREDRAM+1] =temp;
        

        //--------------------------
        //Figure out joy axis to button stuff
        //--------------------------
        if(InputMethod)
        {
            currentmap1 = Head_JD2BMap;
            while (currentmap1 != NULL)
            {
                //This should hopefully always be true:
                if(currentmap1->MyJoystick < MyNumJoysticks)
                {
                    short val = SDL_JoystickGetAxis(
                            MyJoysticks[currentmap1->MyJoystick],
                            currentmap1->MyJoyaxis
                            );

                    if(currentmap1->MyJoydir == POSITIVE_AXIS)
                    {
                        if(val > 16383)
                        {
                            SetPRU_ButState(currentmap1->MyN64but,1);
                        }
                        else
                        {
                            SetPRU_ButState(currentmap1->MyN64but,0);
                        }
                    }
                    else
                    {
                        if(val < -16384)
                        {
                            SetPRU_ButState(currentmap1->MyN64but,1);
                        }
                        else
                        {
                            SetPRU_ButState(currentmap1->MyN64but,0);
                        }
                    }
                }

                currentmap1 = currentmap1->MyNext;
            }
        }


        //--------------------------
        //Process SDL Events
        //--------------------------
        while( SDL_PollEvent( &event ) )
        {
            if(InputMethod == 0)
            {
                //If a key was pressed
                if( event.type == SDL_KEYDOWN )
                {
                    //printf("Got Keydown\n");
                    HandleKeyEvent(event.key.keysym.sym,1);
                    //event.key.keysym.sym is an int
                    //that represents the key
                    //Process key press
                }
                else if( event.type == SDL_KEYUP)
                {
                    HandleKeyEvent(event.key.keysym.sym,0);
                }
            }
            else 
            {
                if( event.type == SDL_JOYBUTTONDOWN)
                {
                    HandleJoybutEvent(event.jbutton.which,
                        event.jbutton.button,1);
                }
                else if( event.type == SDL_JOYBUTTONUP)
                {
                    HandleJoybutEvent(event.jbutton.which,
                        event.jbutton.button,0);
                }
            }

            if( event.type == SDL_QUIT )
            {
                //Quit the program
                printf("Got Quit event\n");
                QuitProgram();
            }
            //If there's an event to handle
        }
    }
    return 0;
}


void QuitProgram()
{
    //Let the PRU know we are stopping
    sharedMem_int[OFFSET_SHAREDRAM] = 0;

    boolCaptureIO = 0;
    boolRunProgram = 0;
    SDL_Quit();


    /* Wait until PRU0 has finished execution */
    printf("\tINFO: Waiting for HALT command.\r\n");
    prussdrv_pru_wait_event (PRU_EVTOUT_0);
    printf("\tINFO: PRU Halted.\r\n");
    prussdrv_pru_clear_event (PRU0_ARM_INTERRUPT);

    printf("0\n");
    /* Disable PRU and close memory mapping*/
    prussdrv_pru_disable(PRU_NUM);
    prussdrv_exit ();
    munmap(ddrMem, 0x0FFFFFFF);
    close(mem_fd);

    printf("1\n");

    //free the mappings
    if(Head_K2BMap != NULL)
    {
        N64Key2ButMap* currentmap0 = Head_K2BMap;
        while(currentmap0->MyNext != NULL)
        {
            N64Key2ButMap* temp = currentmap0;
            currentmap0 = currentmap0->MyNext;
            free(temp);
        }
        if(currentmap0 != NULL)
        {
            free(currentmap0);
        }
    }

    printf("2\n");

    if(Head_K2DMap != NULL)
    {
        N64Key2DirMap* currentmap1 = Head_K2DMap;
        while(currentmap1->MyNext != NULL)
        {
            N64Key2DirMap* temp = currentmap1;
            currentmap1 = currentmap1->MyNext;
            free(temp);
        }
        if(currentmap1 != NULL)
        {
            free(currentmap1);
        }
    }

    printf("3\n");

    if(Head_JB2BMap != NULL)
    {
        Joybut2N64ButMap* currentmap2 = Head_JB2BMap;
        while(currentmap2->MyNext != NULL)
        {
            Joybut2N64ButMap* temp = currentmap2;
            currentmap2 = currentmap2->MyNext;
            free(temp);
        }
        if(currentmap2 != NULL)
        {
            free(currentmap2);
        }
    }

    printf("4\n");

    if(Head_JB2DMap != NULL)
    {
        Joybut2N64DirMap* currentmap3 = Head_JB2DMap;
        while(currentmap3->MyNext != NULL)
        {
            Joybut2N64DirMap* temp = currentmap3;
            currentmap3 = currentmap3->MyNext;
            free(temp);
        }
        if(currentmap3 != NULL)
        {
            free(currentmap3);
        }
    }

    printf("5\n");

    if(Head_JD2BMap != NULL)
    {
        Joydir2N64ButMap* currentmap2 = Head_JD2BMap;
        while(currentmap2->MyNext != NULL)
        {
            Joydir2N64ButMap* temp = currentmap2;
            currentmap2 = currentmap2->MyNext;
            free(temp);
        }
        if(currentmap2 != NULL)
        {
            free(currentmap2);
        }
    }

    printf("6\n");

    if(Head_JD2DMap != NULL)
    {
        Joydir2N64DirMap* currentmap3 = Head_JD2DMap;
        while(currentmap3->MyNext != NULL)
        {
            Joydir2N64DirMap* temp = currentmap3;
            currentmap3 = currentmap3->MyNext;
            free(temp);
        }
        if(currentmap3 != NULL)
        {
            free(currentmap3);
        }
    }

    if(InputMethod == 1)
    {
        printf("Yeah its going to break now:\n");

        //Free the joysticks
        if(MyJoysticks != NULL)
        {
            int i;        
            for( i=0; i < MyNumJoysticks; i++ ) 
            {

                SDL_JoystickClose( MyJoysticks[i] );
            }
            free(MyJoysticks);
        }
    }

}




/*****************************************************************************
* Local Function Definitions                         *
*****************************************************************************/

static int LOCAL_exampleInit (  )
{
    void *DDR_regaddr1, *DDR_regaddr2, *DDR_regaddr3;

    /* open the device */
    mem_fd = open("/dev/mem", O_RDWR);
    if (mem_fd < 0) {
    printf("Failed to open /dev/mem (%s)\n", strerror(errno));
    return -1;
    }

    /* map the DDR memory */
    ddrMem = mmap(0, 0x0FFFFFFF, PROT_WRITE | PROT_READ, MAP_SHARED, mem_fd, DDR_BASEADDR);
    if (ddrMem == NULL) {
    printf("Failed to map the device (%s)\n", strerror(errno));
    close(mem_fd);
    return -1;
    }

    /* Store Addends in DDR memory location */
    DDR_regaddr1 = ddrMem + OFFSET_DDR;
    DDR_regaddr2 = ddrMem + OFFSET_DDR + 0x00000004;
    DDR_regaddr3 = ddrMem + OFFSET_DDR + 0x00000008;

    *(unsigned long*) DDR_regaddr1 = ADDEND1;
    *(unsigned long*) DDR_regaddr2 = ADDEND2;
    *(unsigned long*) DDR_regaddr3 = ADDEND3;

    return(0);
}

static unsigned short LOCAL_examplePassed ( unsigned short pruNum )
{
    unsigned int result_0, result_1, result_2;

     /* Allocate Shared PRU memory. */
    prussdrv_map_prumem(PRUSS0_SHARED_DATARAM, &sharedMem);
    sharedMem_int = (unsigned int*) sharedMem;

    result_0 = sharedMem_int[OFFSET_SHAREDRAM];
    result_1 = sharedMem_int[OFFSET_SHAREDRAM + 1];
    result_2 = sharedMem_int[OFFSET_SHAREDRAM + 2];

    return ((result_0 == ADDEND1) & (result_1 ==  ADDEND2) & (result_2 ==  ADDEND3)) ;

}

