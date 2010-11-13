module Main where

import System
import qualified Data.Map as M
import Data.Maybe
import Language.C
import Language.C.System.GCC
import Language.C.Analysis.AstAnalysis
import Language.C.Analysis.SemRep
import Language.C.Analysis.TravMonad

main = do
  args <- getArgs
  parseFile (head args) (tail args) >>= printGlobalTypes

parseFile :: FilePath -> [String] -> IO CTranslUnit
parseFile input_file cOpts =
  do parse_result <- parseCFile (newGCC "gcc") Nothing cOpts input_file
     case parse_result of
       Left parse_err -> error (show parse_err)
       Right ast      -> return ast

printGlobalTypes transUnit = 
    case runTrav_ (analyseAST transUnit) of
      Left err -> error (show err)
      Right (globals, _) -> mapM_ print $ globalTypeNames globals

globalTypeNames g =
    map identToString $ typeNames ++ sueNames
    where
      typeNames = M.keys (gTypeDefs g)
      sueNames = mapMaybe named (M.keys $ gTags g)
      named (NamedRef x) = Just x
      named _ = Nothing
