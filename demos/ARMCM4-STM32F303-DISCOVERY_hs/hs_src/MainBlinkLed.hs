import Control.Monad
import Control.Concurrent
import Data.Word

import Sleep
import Led

ledGroupA, ledGroupB :: [Word16]
ledGroupA = [led3, led5, led7, led9]
ledGroupB = [led4, led6, led8, led10]

blinkLedLoop :: [Word16] -> Word32 -> IO ()
blinkLedLoop leds sleep = forever $ sequence_ dos
  where
    delays = repeat $ threadDelayMicroseconds sleep
    ledsOnOff = fmap ledOn leds ++ fmap ledOff leds
    dos = concat $ zipWith (\a b -> [a,b]) ledsOnOff delays

main :: IO ()
main = do mapM_ ledOff $ ledGroupA ++ ledGroupB
          forkOS $ blinkLedLoop ledGroupB 50
          blinkLedLoop ledGroupA 170
