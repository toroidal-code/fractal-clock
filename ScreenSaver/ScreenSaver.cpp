//Barebones screensaver example from the-generalist.com

#include <windows.h>
#include <scrnsave.h>
#include <strsafe.h>
#include <winuser.h>

#include <queue>
#include <cmath>
#include <ctime>
#include "GL/gl.h"
#include "GL/glu.h"
#include "resource.h"
#pragma comment (lib, "scrnsavw.lib")
#pragma comment (lib, "comctl32.lib")
#pragma comment (lib, "opengl32.lib")

#define  DLG_SCRNSAVECONFIGURE 2003

#define M_PI 3.14159265358979323846

//define a Windows timer
#define TIMER 1

#define MaxDepth 32 //Far too big for any mortal computer
#define FramesPerSecond 24.
#define ColorAdjustment 0.85

extern "C" {
	_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

// I use these variables to track CPU usage and tune the recursion depth accordingly
static double accumulatedSeconds;
static double accumulatedFrames;
static double framesBetweenDepthChanges;
static unsigned int targetDepth = 15;
static unsigned int viewsCount;
static double totalPixelCount;

typedef double Rotator[2];
float alphaForDepth[MaxDepth];

typedef struct {
	double x;
	double y;
} Point;

typedef struct {
	double width;
	double height;
} Size;

typedef struct {
	Point origin;
	Size size;
} Rect;

static double transition(double now, double transitionSeconds, ...){
	va_list ap;

	double totalSeconds = 0;
	va_start(ap, transitionSeconds);
	while (1) {
		double seconds = va_arg(ap, double);
		if (seconds == 0)
			break;
		totalSeconds += seconds + transitionSeconds; 
		va_arg(ap, double);
	}
	va_end(ap);

	double modnow = fmod(now, totalSeconds);
	double level0;
	va_start(ap, transitionSeconds);
	va_arg(ap, double);
	level0 = va_arg(ap, double);
	va_end(ap);

	double startLevel, endLevel;
	va_start(ap, transitionSeconds);
	while(1){
		double seconds = va_arg(ap,double);
		startLevel = va_arg(ap,double);

		if (modnow < seconds) {			
			endLevel = startLevel;
			break;
		}

		modnow -=seconds;
		if (modnow <= transitionSeconds) {
			seconds = va_arg(ap, double);
			endLevel = (seconds == 0) ? level0 : va_arg(ap,double);
			break;
		}
		modnow -= transitionSeconds;
	}
	va_end(ap);

	if (startLevel == endLevel)
		return startLevel;
	else
	{
		return endLevel + (startLevel - endLevel) * 
			(cos(M_PI * modnow / transitionSeconds) + 1) * .5;
	}
}

/** Set `rotator` to the top half of the rotation matrix 
for a rotation of `rotation` (1 = a full revolution), 
scaled by `scale`. */
static void initRotator(Rotator rotator, double rotation, double scale)
{
	double radians = 2 * M_PI * rotation;
	rotator[0] = cos(radians) * scale;
	rotator[1] = sin(radians) * scale;
}

/** Apply `rotator` to the vector described by `s0`, returning a new vector. */
static inline Size rotateSize(Rotator rotator, Size s0){
   return { 
	   double(s0.width * rotator[0] - s0.height * rotator[1]),
	   double(s0.width * rotator[1] + s0.height * rotator[0])
   };
}

/*** Return the number of seconds since midnight, localtime. */

static double getNow(){
	SYSTEMTIME st;
	GetLocalTime(&st);	
	double now = ((st.wHour * 60) + st.wMinute) * 60 + 
		st.wSecond + (st.wMilliseconds/1000.);
	return now;
}

static double getRotation(double now, double period) {
	return .25 - fmod(now, period) / period;
}

static inline double midX(Rect rect){
	return rect.size.width / 2.;
}

static inline double midY(Rect rect){
	return rect.size.height / 2.;
}

static Rect getRootAndRotators(Rect bounds, Rotator secondRotator, Rotator minuteRotator)
{
	double now = getNow();
	double hourRotation = getRotation(now, 12 * 60 * 60);
	double minuteRotation = getRotation(now, 60 * 60);
	double secondRotation = getRotation(now, 60);

	double scale = transition(now, 12.,
		61., 1.,
		61., 0.793900525984099737375852819636, // cube root of 1/2
		0.);

	initRotator(secondRotator, secondRotation - hourRotation, -scale);
	initRotator(minuteRotator, minuteRotation - hourRotation, -scale);

	Rotator hourRotator;
	initRotator(hourRotator, hourRotation, 1);
	double rootSize = min(bounds.size.width, bounds.size.height)/6.;
	Rect root;
	Size rootSizeStruct = {-rootSize, 0};
	root.size = rotateSize(hourRotator, rootSizeStruct);
	root.origin.x = midX(bounds) - root.size.width;
	root.origin.y = midY(bounds) - root.size.height;
	return root;
}

static void drawBranch(Rect* line, Rotator &r0, Rotator &r1, 
					   unsigned int depth, unsigned int depthLeft, double* color)
{
	Point p2 = {
		line->origin.x + line->size.width,
		line->origin.y + line->size.height
	};

	glColor4f(color[0], color[1], color[2], alphaForDepth[depth]);
	if (depth == 0) {
		glVertex2f(
			line->origin.x + line->size.width * .5,
			line->origin.y + line->size.height * .5
			);
	} else
		glVertex2f(line->origin.x, line->origin.y);
	glVertex2f(p2.x, p2.y);

	if (depthLeft >= 1) {
		Rect newLine;
		newLine.origin = p2;
		double newColor[3] = { color[0], color[1], color[2] };
		newColor[1] = .92 * color[1];

		newLine.size = rotateSize(r0, line->size);
		newColor[0] = ColorAdjustment * color[0];
		newColor[2] = .1 + ColorAdjustment * color[2];
		drawBranch(&newLine, r0, r1, depth + 1, depthLeft - 1, newColor);

		newLine.size = rotateSize(r1, line->size);
		newColor[0] = .1 + ColorAdjustment * color[0];
		newColor[2] = ColorAdjustment * color[2];
		drawBranch(&newLine, r0, r1, depth + 1, depthLeft - 1, newColor);
	}
}


static void InitGL(HWND hWnd, HDC & hDC, HGLRC & hRC)
{
  
  PIXELFORMATDESCRIPTOR pfd;
  ZeroMemory( &pfd, sizeof pfd );
  pfd.nSize = sizeof pfd;
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_GENERIC_ACCELERATED;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 24;
  
  hDC = GetDC( hWnd );
  
  int i = ChoosePixelFormat( hDC, &pfd );  
  SetPixelFormat( hDC, i, &pfd );

  hRC = wglCreateContext( hDC );
  wglMakeCurrent( hDC, hRC );

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_DITHER);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_ALPHA_TEST);
  glAlphaFunc(GL_GREATER, 1./255);

  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glEnable(GL_LINE_SMOOTH);

}
 
static void CloseGL(HWND hWnd, HDC hDC, HGLRC hRC)
{
  wglMakeCurrent( NULL, NULL );
  wglDeleteContext( hRC );
  ReleaseDC( hWnd, hDC );
}

double Log2(double n)
{
	return log(n)/log(2);
}

//Required Function
LRESULT WINAPI ScreenSaverProc(HWND hWnd, UINT message, 
                               WPARAM wParam, LPARAM lParam)
{
	static HDC hDC;
	static HGLRC hRC;
	static RECT rect;
	static Point origin;
	static Size windowSize;
	static Rect windowRect;
	static double rootColor[3] = { 1, 1, 1 };

	switch(message) {
		case WM_CREATE:
			// get window dimensions
			GetClientRect(hWnd, &rect);
			windowSize.height = rect.bottom;
			windowSize.width = rect.right;
			origin.x = windowSize.height / 2.;
			origin.y = windowSize.width / 2.;
			windowRect.origin = origin;
			windowRect.size = windowSize;

			//get configuration from registry if applicable

			//set up OpenGL
			InitGL(hWnd, hDC, hRC);

			//Initialize perspective, viewpoint, and
			//any objects you wish to animate here
			alphaForDepth[0] = 1;
			for (int i = 1; i < MaxDepth; ++i)
				alphaForDepth[i] = pow(i, -1.0);

			//create timer that ticks every 10 ms
			SetTimer( hWnd, TIMER, 15, NULL );
			return(0);
 
		case WM_TIMER:
			//Put your drawing code here
			//This is called every 10 ms
			//time_t startTime = time(NULL);

			Rect bounds = windowRect;


			glViewport(0, 0, bounds.size.width, bounds.size.height);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, bounds.size.width, 0, bounds.size.height, 0, 1);

			glClearColor(0., 0., 0., 1.);
			glClear(GL_COLOR_BUFFER_BIT);
			
			Rotator r0;
			Rotator r1;
			Rect root = getRootAndRotators(bounds, r0, r1);
			
			glLineWidth(2.);
			glBegin(GL_LINES);
			drawBranch(&root, r0, r1, 0, targetDepth, rootColor);
			glEnd();

			glFlush();
			SwapBuffers(hDC);
			
			//accumulatedSeconds += time(NULL) - startTime;
			//++accumulatedFrames;
			return(0);
		
		case WM_DESTROY:
			//Put you cleanup code here.
			//This will be called on close.
			KillTimer(hWnd, TIMER);
			//delete any objects created during animation
			//and close down OpenGL nicely

			CloseGL(hWnd, hDC, hRC);
			return(0);
	}

	return DefScreenSaverProc(hWnd, message, wParam, lParam);
}

#define MINVEL  1                 // minimum redraw speed value     
#define MAXVEL  10                // maximum redraw speed value    
#define DEFVEL  5                 // default redraw speed value    

LONG    lSpeed = DEFVEL;          // redraw speed variable         
extern HINSTANCE hMainInstance;   // screen saver instance handle  
STRSAFE_LPWSTR   szTemp;                // temporary array of characters  
LPCWSTR szRedrawSpeed = L"Redraw Speed";   // .ini speed entry 

//Required Function
BOOL WINAPI ScreenSaverConfigureDialog(HWND hDlg, UINT message, 
									   WPARAM wParam, LPARAM lParam)
{
	static HWND hSpeed;   // handle to speed scroll bar 
	static HWND hOK;      // handle to OK push button  
	static HRESULT hr;

	switch (message)
	{
	case WM_INITDIALOG:

		// Retrieve the application name from the .rc file.  
		LoadString(hMainInstance, idsAppName, szAppName,
			80 * sizeof(TCHAR));

		// Retrieve the .ini (or registry) file name. 
		LoadString(hMainInstance, idsIniFile, szIniFile,
			MAXFILELEN * sizeof(TCHAR));

		// TODO: Add error checking to verify LoadString success
		//       for both calls.

		// Retrieve any redraw speed data from the registry. 
		lSpeed = GetPrivateProfileInt(szAppName, szRedrawSpeed,
			DEFVEL, szIniFile);

		// If the initialization file does not contain an entry 
		// for this screen saver, use the default value. 
		if (lSpeed > MAXVEL || lSpeed < MINVEL)
			lSpeed = DEFVEL;

		// Initialize the redraw speed scroll bar control.
		hSpeed = GetDlgItem(hDlg, ID_SPEED);
		SetScrollRange(hSpeed, SB_CTL, MINVEL, MAXVEL, FALSE);
		SetScrollPos(hSpeed, SB_CTL, lSpeed, TRUE);

		// Retrieve a handle to the OK push button control.  
		hOK = GetDlgItem(hDlg, ID_OK);

		return TRUE;

	case WM_HSCROLL:

		// Process scroll bar input, adjusting the lSpeed 
		// value as appropriate. 
		switch (LOWORD(wParam))
		{
		case SB_PAGEUP:
			--lSpeed;
			break;

		case SB_LINEUP:
			--lSpeed;
			break;

		case SB_PAGEDOWN:
			++lSpeed;
			break;

		case SB_LINEDOWN:
			++lSpeed;
			break;

		case SB_THUMBPOSITION:
			lSpeed = HIWORD(wParam);
			break;

		case SB_BOTTOM:
			lSpeed = MINVEL;
			break;

		case SB_TOP:
			lSpeed = MAXVEL;
			break;

		case SB_THUMBTRACK:
		case SB_ENDSCROLL:
			return TRUE;
			break;
		}

		if ((int)lSpeed <= MINVEL)
			lSpeed = MINVEL;
		if ((int)lSpeed >= MAXVEL)
			lSpeed = MAXVEL;

		SetScrollPos((HWND)lParam, SB_CTL, lSpeed, TRUE);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_OK:

			// Write the current redraw speed variable to
			// the .ini file. 
			hr = StringCchPrintf(szTemp, 20, L"%ld", lSpeed);
			if (SUCCEEDED(hr))
				WritePrivateProfileString(szAppName, szRedrawSpeed,
					szTemp, szIniFile);

		case ID_CANCEL:
			EndDialog(hDlg, LOWORD(wParam) == ID_OK);

			return TRUE;
		}
	}
	return FALSE;
}

//Required Function by SCRNSAVE.LIB
BOOL WINAPI RegisterDialogClasses(HANDLE hInst)
{
	return(TRUE);
}

