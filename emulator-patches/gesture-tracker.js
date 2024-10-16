// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Gesture tracking based on xwiper, licensed under the MIT license
// https://github.com/behnammodi/xwiper/blob/master/LICENSE

// Copyright (c) 2019 Behnam Mohammadi (بهنام محمدی)
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

const THRESHOLD = 50;

class GestureTracker {
  constructor(element) {
    this.element = element;
    this.touchStartX = 0;
    this.touchStartY = 0;
    this.touchEndX = 0;
    this.touchEndY = 0;

    this.onTouchStart = this.onTouchStart.bind(this);
    this.onTouchEnd = this.onTouchEnd.bind(this);

    this.element.addEventListener(
      'touchstart',
      this.onTouchStart,
      { passive: true },
    );
    this.element.addEventListener(
      'touchend',
      this.onTouchEnd,
      { passive: true },
    );
  }

  onTouchStart(event) {
    this.touchStartX = event.changedTouches[0].screenX;
    this.touchStartY = event.changedTouches[0].screenY;
  }

  onTouchEnd(event) {
    this.touchEndX = event.changedTouches[0].screenX;
    this.touchEndY = event.changedTouches[0].screenY;
    this.handleGesture();
  }

  destroy() {
    this.element.removeEventListener('touchstart', this.onTouchStart);
    this.element.removeEventListener('touchend', this.onTouchEnd);
  }

  // swipe-left
  // swipe-right
  // swipe-up
  // swipe-down
  // tap
  addEventListener(eventName, callback, options) {
    this.element.addEventListener(eventName, callback, options);
  }

  removeEventListener(eventName, callback) {
    this.element.removeEventListener(eventName, callback);
  }

  trigger(eventName) {
    console.log(eventName);
    this.element.dispatchEvent(new Event(eventName));
  }

  handleGesture() {
    if (this.touchEndX + THRESHOLD <= this.touchStartX) {
      this.trigger('swipe-left');
    } else if (this.touchEndX - THRESHOLD >= this.touchStartX) {
      this.trigger('swipe-right');
    } else if (this.touchEndY + THRESHOLD <= this.touchStartY) {
      this.trigger('swipe-up');
    } else if (this.touchEndY - THRESHOLD >= this.touchStartY) {
      this.trigger('swipe-down');
    } else if (this.touchEndY === this.touchStartY) {
      this.trigger('tap');
    }
  }
}

window.GestureTracker = GestureTracker;
