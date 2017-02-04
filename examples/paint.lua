--[[
Simple paint program to demonstrate lcd & touch on ILI9341 based displays
We are using Lua coroutines so that long running program does not block
Execute paint.run() to start the program
]]--

if tft.gettype() ~= 1 then
    print("LCD not initialized or wrong type")
    return
end

paint = {}

paint.orient = tft.PORTRAIT_FLIP
paint.running = false
paint.waittouch = 0
-- ----------------------------------------------------------

-- =================================================
-- ---------------------------
function paint.wait(tpwait)
	paint.waittouch = tpwait
	local touch
	while true do
		touch, _, _ = tft.gettouch()
		if ((touch > 0) and (paint.waittouch == 1)) or ((touch == 0) and (paint.waittouch == 0)) then
			break
		end
		tmr.sleepms(10)
	end
end
-- =================================================


-- Display selection bars and some info
-- ------------------------
function paint.paint_info()
  local dispx, dispy, dx, dy, dw, dh, fp

  tft.setorient(paint.orient)
  tft.setfont(tft.FONT_DEFAULT)
  tft.setrot(0)
  tft.setfixed(0)
  dispx, dispy = tft.getscreensize()
  dx = dispx // 8
  dw,dh = tft.getfontsize()
  dy = dispy - dh - 5
  fp = math.ceil((dx // 2) - (dw // 2))
  
  tft.rect(dx*0,0,dx-2,18,tft.BLACK,tft.BLACK)
  tft.rect(dx*1,0,dx-2,18,tft.WHITE,tft.WHITE)
  tft.rect(dx*2,0,dx-2,18,tft.RED,tft.RED)
  tft.rect(dx*3,0,dx-2,18,tft.GREEN,tft.GREEN)
  tft.rect(dx*4,0,dx-2,18,tft.BLUE,tft.BLUE)
  tft.rect(dx*5,0,dx-2,18,tft.YELLOW,tft.YELLOW)
  tft.rect(dx*6,0,dx-2,18,tft.CYAN,tft.CYAN)
  tft.rect(dx*7,0,dx-2,18,tft.ORANGE,tft.ORANGE)

  tft.rect(dx*7+2,2,dx-6,14,tft.WHITE, tft.ORANGE)
  tft.rect(dx*7+3,3,dx-8,12,tft.BLACK, tft.ORANGE)
  
  tft.rect(dx*0,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*1,dy,dx-2,dispy-dy,tft.YELLOW)
  tft.rect(dx*2,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*3,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*4,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*5,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*6,dy,dx-2,dispy-dy,tft.DARKGREY)
  tft.rect(dx*7,dy,dx-2,dispy-dy,tft.DARKGREY)
  
  tft.setcolor(tft.CYAN)
  tft.write(dx*0+fp,dy+3,"2")
  tft.write(dx*1+fp,dy+3,"4")
  tft.write(dx*2+fp,dy+3,"6")
  tft.write(dx*3+fp,dy+3,"8")
  tft.write(dx*4+fp-3,dy+3,"10")
  --tft.write(dx*5+fp,dy+3,"S")
  tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, tft.LIGHTGREY, tft.LIGHTGREY)
  tft.write(dx*6+fp,dy+3,"C")
  tft.write(dx*7+fp,dy+3,"R")
  
  tft.setcolor(tft.YELLOW)
  tft.write(60,40,"S")
  tft.setcolor(tft.CYAN)
  tft.write(tft.LASTX,tft.LASTY," change shape")
  tft.circle(60+(dw // 2), 40+(dh // 2), dh // 2 + 1, tft.LIGHTGREY, tft.LIGHTGREY)

  tft.setcolor(tft.YELLOW)
  tft.write(60,dh*2+40,"C")
  tft.setcolor(tft.CYAN)
  tft.write(tft.LASTX,tft.LASTY," Clear screen")

  tft.setcolor(tft.YELLOW)
  tft.write(60,dh*4+40,"R")
  tft.setcolor(tft.CYAN)
  tft.write(tft.LASTX,tft.LASTY," Return, exit program")

  tft.setcolor(tft.YELLOW)
  tft.write(60,dh*6+40,"2,4,6,8")
  tft.setcolor(tft.CYAN)
  tft.write(tft.LASTX,tft.LASTY," draw size")

  paint.wait(1)
  
  tft.rect(0,20,dispx,dy-20,tft.BLACK,tft.BLACK)
  
  return dx, dy
end

-- Paint main loop
-- ---------------------
function paint.dopaint()
  local dispx, dispy, dx, dy, x, y, touch, lx, lx, first, drw, dodrw, color, lastc, lastr

  paint.running = true
  
  dx, dy = paint.paint_info()
  dispx, dispy = tft.getscreensize()
  
  first = true
  drw = 1
  color = tft.ORANGE
  lastc = dx*7
  r = 4
  lastr = dx
  
  while true do
	tmr.sleepms(10)
	-- get touch status and coordinates
    touch, x, y = tft.gettouch()
    if touch > 0 then
        dodrw = true
		
        if first and (y < 20) then
            -- === upper bar touched, color select ===
            tft.rect(lastc,0,dx-2,18,color,color)
            if x > (dx*7) then
                color = tft.ORANGE
                lastc = dx*7
            elseif x > (dx*6) then
                color = tft.CYAN
                lastc = dx*6
            elseif x > (dx*5) then
                color = tft.YELLOW
                lastc = dx*5
            elseif x > (dx*4) then
                color = tft.BLUE
                lastc = dx*4
            elseif x > (dx*3) then
                color = tft.GREEN
                lastc = dx*3
            elseif x > (dx*2) then
                color = tft.RED
                lastc = dx*2
            elseif x > dx then
                color = tft.WHITE
                lastc = dx
            elseif x > 1 then
                color = tft.BLACK
                lastc = 0
            end
			tft.rect(lastc+2,2,dx-6,14,tft.WHITE,color)
			tft.rect(lastc+3,3,dx-8,12,tft.BLACK,color)
			-- wait for touch release
			paint.wait(0)
            first = true

        elseif first and (y > dy) then
            -- === lower bar touched, size, r, erase shape select, return ===
            if x < (dx*5) then
                tft.rect(lastr,dy,dx-2,dispy-dy,tft.DARKGREY)
            end
            if x > (dx*7) then
                break
            elseif x > (dx*6) then
				-- clear drawing area
                tft.rect(0,20,dispx,dy-20,tft.BLACK,tft.BLACK)
            elseif x > (dx*5) then
				-- change drawing shape
				drw = drw + 1
                if drw > 4 then
                    drw = 1
                end
				tft.rect(dx*5,dy,dx-2,dispy-dy,tft.DARKGREY, tft.BLACK)
                if drw == 1 then
                    tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, tft.LIGHTGREY, tft.LIGHTGREY)
                elseif drw == 3 then
                    tft.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, tft.LIGHTGREY, tft.LIGHTGREY)
                elseif drw == 2 then
                    tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, tft.YELLOW, tft.DARKGREY)
                elseif drw == 4 then
                    tft.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, tft.YELLOW, tft.DARKGREY)
                end
			-- drawing size
            elseif x > (dx*4) then
                r = 10
                lastr = dx*4
            elseif x > (dx*3) then
                r = 8
                lastr = dx*3
            elseif x > (dx*2) then
                r = 6
                lastr = dx*2
            elseif x > dx then
                r = 4
                lastr = dx
            elseif x > 0 then
                r = 2
                lastr = 0
            end
            if x < (dx*5) then
                tft.rect(lastr,dy,dx-2,dispy-dy,tft.YELLOW)
            end
			-- wait for touch release
			paint.wait(0)
            first = true

        elseif (x > r) and (y > (r+20)) and (y < (dy-r)) then
            -- === touch on drawing area, draw shape ===
            if first ~= true then
                if (math.abs(x-lx) > 5) or (math.abs(y-ly) > 5) then
                    dodrw = false
                end
            end
            
            if dodrw then
                if drw == 1 then
                    tft.circle(x, y, r, color, color)
                elseif drw == 3 then
                    tft.rect(x-r, y-r, r*2, r*2, color, color)
                elseif drw == 2 then
                    tft.circle(x, y, r, tft.DARKGREY, color)
                elseif drw == 4 then
                    tft.rect(x-r, y-r, r*2, r*2, tft.DARKGREY, color)
                end
            end
            -- save touched coordinates
            lx = x
            ly = y
            first = false
        end
    else
      first = true
	  paint.wait(1)
    end
  end
  
  tft.rect(0,dy,dispx,dispy-dy,tft.YELLOW,tft.BLACK)
  tft.write(tft.CENTER, dy+3, "FINISHED")
  
  paint.waittouch = 0
  paint.running = false
end

-- -----------------------
function paint.run(orient)
	if paint.running then
		print("Already running")
	else
		if orient ~= nil then
			paint.orient = orient
		end
		paint.waittouch = 0
		paint.dopaint()
	end
end

