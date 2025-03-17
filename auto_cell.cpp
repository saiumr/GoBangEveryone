/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <cassert>
#include <memory>
#include <limits>
#include <chrono>
#include <random>

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

using CellShape = SDL_Rect;
using CellShake = SDL_FPoint;
#define MAX_CELL_COUNT 16384



// This header-only Random namespace implements a self-seeding Mersenne Twister.
// Requires C++17 or newer.
// It can be #included into as many code files as needed (The inline keyword avoids ODR violations)
// Freely redistributable, courtesy of learncpp.com (https://www.learncpp.com/cpp-tutorial/global-random-numbers-random-h/)
namespace Random
{
	inline std::mt19937 generate()
	{
		std::random_device rd{};

		// Create seed_seq with clock and 7 random numbers from std::random_device
		std::seed_seq ss{
			static_cast<std::seed_seq::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()),
				rd(), rd(), rd(), rd(), rd(), rd(), rd() };

		return std::mt19937{ ss };
	}

	inline std::mt19937 mt{ generate() }; // generates a seeded std::mt19937 and copies it into our global object

	inline int get(int min, int max)
	{
		return std::uniform_int_distribution{min, max}(mt);
	}

	template <typename T>
	T get(T min, T max)
	{
		return std::uniform_int_distribution<T>{min, max}(mt);
	}

	template <typename R, typename S, typename T>
	R get(S min, T max)
	{
		return get<R>(static_cast<R>(min), static_cast<R>(max));
	}
}

class Cell {
public:
  Cell() = default;
  Cell(CellShape shape, bool is_active) : shape_{shape_}, is_active_{is_active} {}
  bool get_active_state() { return is_active_; }
  Cell& set_active_state(bool s) { is_active_ = s; return *this; }
  bool get_wait_state() { return wait_for_select_; }
  Cell& set_wait_state(bool s) { wait_for_select_ = s; return *this; }
  CellShape get_shape() { return shape_; }
  Cell& set_shape(CellShape shape) { shape_ = shape; return *this; }
  CellShake get_shake() { return shake_; }
  Cell& set_shake(CellShake s) { shake_ = s; return *this; }
  bool  get_active_change() { return active_change; }
  Cell& set_active_change(bool s) { active_change = s; return *this; }

private:
  bool      is_active_ {false};
  bool      wait_for_select_ {false};
  bool      active_change {false};
  CellShape shape_;

  // active has [-1, 1] value
  CellShake shake_ {0.0f, 0.0f};
};

class CellGrand {
public:
  CellGrand(const CellGrand&) = delete;
  CellGrand(int side, int w, int h): side_{side}, w_{w}, h_{h} {
    check_valid();
    SDL_GetRenderScale(renderer, &scale_x_, &scale_y_);
    init_cells_();
  }

  void handle_input(SDL_Event *event) {
    float mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    mouse_x /= scale_x_;
    mouse_y /= scale_y_;
    for(int i = 0; i < cell_count_; ++i) {
      if (mouse_x >= cells_[i].get_shape().x && mouse_x <= cells_[i].get_shape().x + side_ &&
          mouse_y >= cells_[i].get_shape().y && mouse_y <= cells_[i].get_shape().y + side_)
      {
        cells_[i].set_wait_state(true);
      }
      else
      {
        cells_[i].set_wait_state(false);
      }

      if (!start_ && cells_[i].get_wait_state() && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (!cells_[i].get_active_state()) {
          cells_[i].set_active_state(true);
          ready_++;
        } else {
          cells_[i].set_active_state(false);
          ready_--;
        }
      }
    }

    if (ready_ > 0 && event->type == SDL_EVENT_KEY_DOWN) {
      if (event->key.scancode == SDL_SCANCODE_RETURN) {
        start_ = !start_;
      }
    }

    if (start_ && ready_ <= 0) {
      start_ = false;
      ready_ = 0;
    }
  }

  bool play() {
    update_();
    ai_();
    draw_cells_();
    return start_;
  }

  int get_side() const { return side_; }
  int get_w()    const { return w_; }
  int get_h()    const { return h_; }

private:
  void check_valid() { assert(side_>=3 && "error: side must be >= 3 pixels"); }
  int side_;
  int w_;
  int h_;
  Cell cells_[MAX_CELL_COUNT];
  int cell_count_;
  float scale_x_;
  float scale_y_;
  int   ready_ {0};
  bool  start_ {false};


  void ai_() {
    int check_around {0};

    for (int i = 0; i < w_; ++i) {
      for (int j = 0; j < h_; ++j) {

        if (!start_ && cells_[i * w_ + j].get_active_state()) {
          cells_[i * w_ + j].set_shake({static_cast<float>(Random::get(-1, 1)), static_cast<float>(Random::get(-1, 1))});
        } else {
          cells_[i * w_ + j].set_shake({0.0f, 0.0f});
        }

        // motion
        if (start_) {
          if (i>0&&j>0 && cells_[(i-1) * w_ + (j-1)].get_active_state() ) { check_around++; }
          if (j>0 && cells_[i * w_ + (j-1)].get_active_state() ) { check_around++; }
          if (i<w_&&j>0 && cells_[(i+1) * w_ + (j-1)].get_active_state() ) { check_around++; }
          if (i<w_ && cells_[(i+1) * w_ + j].get_active_state() ) { check_around++; }
          if (i<w_&&j<h_ && cells_[(i+1) * w_ + (j+1)].get_active_state() ) { check_around++; }
          if (j<h_ && cells_[i * w_ + (j+1)].get_active_state() ) { check_around++; }
          if (i>0&&j<h_ && cells_[(i-1) * w_ + (j+1)].get_active_state() ) { check_around++; }
          if (i>0 && cells_[(i-1) * w_ + j].get_active_state() ) { check_around++; }

          if (!cells_[i * w_ + j].get_active_state()) {
            if (check_around == 3) {
              cells_[i * w_ + j].set_active_change(true);
              ready_++;
            }
          } else {
            if (check_around < 2 || check_around > 3) {
              cells_[i * w_ + j].set_active_change(true);
              ready_--;
            }
          }

        }

      }
    }

    SDL_Log("cells: %d", ready_);

    for (int i = 0; i < cell_count_; ++i) {
      if(cells_[i].get_active_change()) {
        cells_[i].set_active_state(!cells_[i].get_active_state());
        cells_[i].set_active_change(false);
      }
    }

  }

  void draw_cells_() {
    SDL_Color origin_color;
    SDL_GetRenderDrawColor(renderer, &origin_color.r, &origin_color.g, &origin_color.b, &origin_color.a);

    const SDL_Color kWaitColor   {97, 175, 239, 188};
    const SDL_Color kActiveColor {97, 175, 239, 255};
    
    for(int i = 0; i < cell_count_; ++i) {
      SDL_Rect  rect {static_cast<SDL_Rect>(cells_[i].get_shape())};
      SDL_FRect frect{};
      SDL_RectToFRect(&rect, &frect);
      
      // fill active/wait shape
      if (cells_[i].get_active_state()) {
        SDL_SetRenderDrawColor(renderer, kActiveColor.r, kActiveColor.g, kActiveColor.b, kActiveColor.a);
        CellShake s = cells_[i].get_shake();
        frect.x += s.x;
        frect.y += s.y;
        SDL_RenderFillRect(renderer, &frect);
      } else if (cells_[i].get_wait_state()) {
        SDL_SetRenderDrawColor(renderer, kWaitColor.r, kWaitColor.g, kWaitColor.b, kWaitColor.a);
        SDL_RenderFillRect(renderer, &frect);
      }

      // draw shape
      SDL_SetRenderDrawColor(renderer, origin_color.r, origin_color.g, origin_color.b, origin_color.a);
      SDL_RenderRect(renderer, &frect);
    }
  }

  void update_() {
    float scale_x, scale_y;
    SDL_GetRenderScale(renderer, &scale_x, &scale_y);
    if (std::abs(scale_x_ - scale_x) < std::numeric_limits<float>::epsilon() && 
        std::abs(scale_y_ - scale_y) < std::numeric_limits<float>::epsilon() )
    {
      return ;
    }
    scale_x_ = scale_x;
    scale_y_ = scale_y;

    init_cells_();
  }

  void init_cells_() {
    SDL_Point start_pos;
    const int kGap = 2;  // pixels

    int window_w {};
    int window_h {};
    SDL_GetWindowSize(window, &window_w, &window_h);
    window_w /= scale_x_;
    window_h /= scale_y_;

    int width {side_ * w_ + kGap * (w_-1)};
    if (width > window_w - (4 * kGap + 2 * side_)) {
      start_pos.x = 2 * kGap;
      w_ = (window_w - 2) / (2 + side_);
    } else {
      start_pos.x = (window_w - width) / 2;
      w_ = (width + 2) / (side_ + 2);
    }

    int height {side_ * h_ + kGap * (h_-1)};
    if (height > window_h - (4 * kGap + 2 * side_)) {
      start_pos.y = 2 * kGap;
      h_ = (window_h - 2) / (2 + side_);
    } else {
      start_pos.y = (window_h - height) / 2;
      h_ = (height + 2) / (side_ + 2);
    }

    cell_count_ = w_ * h_;

    assert(cell_count_ <= MAX_CELL_COUNT && "cell_count_ > max limit");

    for (int i = 0; i < w_; ++i) {
      for (int j = 0; j < h_; ++j) {
        cells_[i * w_ + j].set_shape({start_pos.x + i*(side_+kGap), start_pos.y + j*(side_+kGap), side_, side_})
                          .set_wait_state(false)
                          .set_active_state(false)
                          .set_active_change(false);
      }
    }
  }

};

std::unique_ptr<CellGrand> gCG;

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    /* Create the window */
    if (!SDL_CreateWindowAndRenderer("Auto Cell", 800, 600, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    gCG = std::make_unique<CellGrand>(8, 25, 25);
    return SDL_APP_CONTINUE;
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }

    if (event->type == SDL_EVENT_KEY_DOWN) {
      if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return SDL_APP_SUCCESS;
      }
    }

    gCG->handle_input(event);
    return SDL_APP_CONTINUE;
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    const char *message = "Auto Cell";
    int w = 0, h = 0;
    float x, y;
    const float scale = 2.0f;

    const int FPS = 2; // expect fps
    const float frameTime = 1.0f / FPS;
    Uint64 startTicks;
    Uint64 frameTicks;
    Uint64 frequency = SDL_GetPerformanceFrequency();

    startTicks = SDL_GetPerformanceCounter();
    /* Center the message and scale it up */
    SDL_GetRenderOutputSize(renderer, &w, &h);
    SDL_SetRenderScale(renderer, scale, scale);
    x = ((w / scale) - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * SDL_strlen(message)) / 2;
    // y = ((h / scale) - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2;
    y = 10.0f;

    /* Draw the message */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(renderer, x, y, message);
    bool status = gCG->play();
    SDL_RenderPresent(renderer);

    frameTicks = SDL_GetPerformanceCounter() - startTicks;
    float deltaTime = static_cast<float>(frameTicks) / frequency;
    if (status && deltaTime < frameTime) {
      SDL_Delay(static_cast<Uint32>((frameTime - deltaTime) * 1000.0f));
    }

    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
}
