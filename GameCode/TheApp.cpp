//=====================================================
// TheApp.cpp
// by Andrew Socha
//=====================================================

#include "TheApp.hpp"
#include "Engine/Time/Time.hpp"
#include "Engine/Renderer/OpenGLRenderer.hpp"
#include "World.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Sound/SoundSystem.hpp"

const float TEX_COORD_SIZE_PER_TILE = 1.0f / 32.0f;

///=====================================================
/// 
///=====================================================
TheApp::TheApp()
:m_isRunning(true),
m_world(0),
m_renderer(0),
m_inputSystem(0),
m_soundSystem(0){
}

///=====================================================
/// 
///=====================================================
TheApp::~TheApp(){
}

///=====================================================
/// 
///=====================================================
void TheApp::Startup(void* windowHandle){
	m_windowHandle = windowHandle;

	InitializeTimer();

	m_inputSystem = new InputSystem();
	if (m_inputSystem){
		m_inputSystem->Startup(windowHandle);
		m_inputSystem->ShowMouse(false);
	}

	m_soundSystem = new SoundSystem();
	if (m_soundSystem)
		m_soundSystem->Startup();

	m_renderer = new OpenGLRenderer();
	if (m_renderer){
		m_renderer->Startup((HWND)windowHandle);
		m_renderer->InitializeAdvancedOpenGLFunctions();
		m_renderer->SetAlphaTest(true);
		m_renderer->IgnoreEmptyPixels();

 		m_world = new World();
 		m_world->Startup();
	}
}

///=====================================================
/// 
///=====================================================
void TheApp::Run(){
	while(m_isRunning){
		ProcessInput();
		Update();
		RenderWorld();
	}
}

///=====================================================
/// 
///=====================================================
void TheApp::Shutdown(){
	if (m_world){
		m_world->Shutdown(m_renderer);
		delete m_world;
	}

	if (m_renderer){
		m_renderer->Shutdown();
		delete m_renderer;
	}

	if (m_inputSystem){
		m_inputSystem->Shutdown();
		delete m_inputSystem;
	}

	if (m_soundSystem){
		m_soundSystem->Shutdown();
		delete m_soundSystem;
	}
}

///=====================================================
/// 
///=====================================================
void TheApp::ProcessInput(){
	if (m_inputSystem){
		m_inputSystem->Update();
		if (m_inputSystem->IsKeyDown(VK_ESCAPE) || m_inputSystem->IsReadyToQuit())
			m_isRunning = false;
	}
}

///=====================================================
/// 
///=====================================================
void TheApp::Update(){
	if (m_soundSystem){
		m_soundSystem->Update();
	}

	double currentTime = GetCurrentSeconds();
	static double lastTime = currentTime;
	double deltaSeconds = currentTime - lastTime;

	if (deltaSeconds > 0.5) deltaSeconds = 0.5;

	lastTime = currentTime;

	if (m_world){
		m_world->Update(deltaSeconds, m_renderer);

		if (!m_world->IsRunning())
			m_isRunning = false;
	}
}

///=====================================================
/// 
///=====================================================
void TheApp::RenderWorld() const{
	m_renderer->ClearBuffer();

	m_renderer->SetPerspectiveView();
	m_renderer->SetDepthTest(true);
	m_renderer->SetCulling(true);

	if (m_world)
		m_world->Draw(m_renderer);

	m_renderer->SwapBuffers();
}